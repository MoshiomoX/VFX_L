// ============================================================
// WeaponSystem.cpp
// ============================================================
#include "ECS/System/WeaponSystem.h"
#include "ECS/Registry.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/ModelComponent.h"
#include "Component/Projectile/ProjectileComponent.h"
#include "Component/WandComponent.h"
#include "Component/ManaComponent.h"
#include "Collider/CollisionSystem.h"
#include "ECS/View.h"
#include <algorithm>

#include "Component/Projectile/ProjectileVisualComponent.h"
using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Matrix;

void WeaponSystem::SetProjectileModel(ItemID id, std::shared_ptr<Model> m)
{
    for (auto& p : m_Models)
    {
        if (p.first == id) { p.second = m; return; }
    }
    m_Models.push_back({ id, m });
}

std::shared_ptr<Model> WeaponSystem::GetModel(ItemID id) const
{
    for (const auto& p : m_Models)
        if (p.first == id) return p.second;
    return nullptr;
}

// ============================================================
// 1回の施法ぶんの発射要求を積む（分裂の扇状展開はここ）
// ============================================================
void WeaponSystem::QueueOneCast(const SpellStats& s,
    const Vector3& muzzle, const Vector3& dir)
{
    int count = (std::max)(1, s.projectileCount);

    if (count == 1 || s.spreadAngle <= 0.0f)
    {
        // 単発: そのまま積む
        m_Requests.push_back({ s.id, muzzle, dir,
            s.speed, s.radius, s.damage, s.lifetime });
        return;
    }

    // 分裂: spreadAngle を count 等分し、Y 軸まわりに扇状へ広げる
    //  例) count=3, spread=30 → -15°, 0°, +15°
    float step = s.spreadAngle / (float)(count - 1);
    float start = -s.spreadAngle * 0.5f;

    for (int i = 0; i < count; ++i)
    {
        float deg = start + step * (float)i;
        Matrix rot = Matrix::CreateRotationY(DirectX::XMConvertToRadians(deg));
        Vector3 d = Vector3::TransformNormal(dir, rot);
        d.Normalize();

        m_Requests.push_back({ s.id, muzzle, d,
            s.speed, s.radius, s.damage, s.lifetime });
    }
}

// ============================================================
// Update
// ※マナは書かない。CanAfford で確認して Reserve で予約するだけ。
//   引き落としと回復は ManaSystem がこの後で行う。
// ============================================================
void WeaponSystem::Update(Registry& reg, float dt, const CollisionSystem& collision)
{
    m_Requests.clear();
    m_Spawned.clear();


    reg.CreateView<TransformComponent, WandComponent, ManaComponent>()
        .Each([&](Entity e, TransformComponent& tf, WandComponent& wand, ManaComponent& mana)
            {
                // ---- 施法アニメ用のタイマーを進める ----
                if (wand.castAnimTimer > 0.0f)
                    wand.castAnimTimer -= dt;

                Vector3 muzzle = tf.position + wand.muzzleOffset;

                // ---- 索敵は1回だけ（全ての出力源が同じ方向を向く）----
                Entity target = 0;
                bool hasTarget = collision.FindNearestEntity(
                    muzzle, wand.range, Layer_Enemy, target);

                Vector3 aimDir(0, 0, 1);
                if (hasTarget)
                {
                    Vector3 targetPos = reg.Get<TransformComponent>(target).position;
                    if (reg.Has<ColliderComponent>(target))
                        targetPos += reg.Get<ColliderComponent>(target).offset;

                    aimDir = targetPos - muzzle;
                    if (aimDir.LengthSquared() < 1e-6f) hasTarget = false;
                    else aimDir.Normalize();
                }

                // ---- 発射の許可をモードで決める ----
                // ※pendingCasts はモードに関係なく消化する。
                //   一度始めた連発は最後まで撃ち切る（1回の施法 = 1 combo）。
                bool allowNewCast = true;
                bool ignoreCooldown = false;

                switch (wand.castMode)
                {
                case CastMode::Auto:
                    allowNewCast = true;
                    break;
                case CastMode::Manual:
                    allowNewCast = wand.castRequested;
                    break;
                case CastMode::DebugBurst:
                    allowNewCast = true;
                    ignoreCooldown = true;
                    break;
                }

                // ---- 出力源ごとに独立して処理する ----
                for (auto& s : wand.spells)
                {
                    // === 連発の続き（二重釈放の残り）===
                    if (s.pendingCasts > 0)
                    {
                        s.delayTimer -= dt;
                        if (s.delayTimer <= 0.0f)
                        {
                            if (hasTarget && mana.CanAfford(s.manaCost))
                            {
                                QueueOneCast(s, muzzle, aimDir);
                                mana.Reserve(s.manaCost);
                                wand.castAnimTimer = wand.castAnimDuration;
                            }
                            --s.pendingCasts;
                            s.delayTimer = s.castDelay;
                        }
                        continue;   // 連発中は新しい施法を始めない
                    }

                    // === 新しい施法 ===
                    // ※castTimer は撃てなくても減らし続ける。
                    //   標的が現れた瞬間に撃てるようにするため。
                    s.castTimer -= dt;
                    if (!ignoreCooldown && s.castTimer > 0.0f) continue;
                    if (!allowNewCast) continue;
                    if (!hasTarget) continue;
                    if (!mana.CanAfford(s.manaCost)) continue;

                    QueueOneCast(s, muzzle, aimDir);
                    mana.Reserve(s.manaCost);
                    wand.castAnimTimer = wand.castAnimDuration;

                    // 二重釈放: 残りの回数を pending として積んでおく
                    s.pendingCasts = (std::max)(0, s.castCount - 1);
                    s.delayTimer = s.castDelay;
                    s.castTimer = s.castInterval;
                }
            });

    // ---- 走査が終わってから Entity を作る ----
    for (const auto& req : m_Requests)
    {
        Entity p = reg.Create();

        TransformComponent tf;
        tf.position = req.muzzle;
        reg.Add<TransformComponent>(p, tf);

        ColliderComponent col;
        col.shape = ColliderShape::Sphere;
        col.radius = req.radius;
        col.layer = Layer_PlayerShot;
        col.mask = Layer_Enemy | Layer_Terrain;
        reg.Add<ColliderComponent>(p, col);

        ProjectileComponent pj;
        pj.velocity = req.dir * req.speed;
        pj.damage = req.damage;
        pj.lifetime = req.lifetime;
        reg.Add<ProjectileComponent>(p, pj);
        m_Spawned.push_back({ p, req.id });
        // ※Rigidbody は付けない（position は ProjectileSystem が直接進める）

        if (auto m = GetModel(req.id))
        {
            ModelComponent mc;
            mc.model = m;
            reg.Add<ModelComponent>(p, mc);

        }
        if (const auto* v = FindVisual(req.id))
        {
            ProjectileVisualComponent vis;
            vis.size = v->size;
            vis.color = v->color;
            vis.stretch = v->stretch;
            reg.Add<ProjectileVisualComponent>(p, vis);
        }
    }
}
void WeaponSystem::SetProjectileVisual(ItemID id, float size,
    const DirectX::SimpleMath::Vector4& color, float stretch)
{
    for (auto& v : m_Visuals)
    {
        if (v.id == id) { v.size = size; v.color = color; v.stretch = stretch; return; }
    }
    m_Visuals.push_back({ id, size, color, stretch });
}

const WeaponSystem::VisualDef* WeaponSystem::FindVisual(ItemID id) const
{
    for (const auto& v : m_Visuals)
        if (v.id == id) return &v;
    return nullptr;
}