// ============================================================
// WeaponSystem.cpp
// ============================================================
#include "WeaponSystem.h"
#include "Registry.h"
#include "TransformComponent.h"
#include "ColliderComponent.h"
#include "ModelComponent.h"
#include "ProjectileComponent.h"
#include "WandComponent.h"
#include "CollisionSystem.h"
#include "View.h"
#include <algorithm>

#include "ProjectileVisualComponent.h"
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
// 1回の施法ぶんを積む。分裂はここで扇状に展開する（空間展開）
// ============================================================
void WeaponSystem::QueueOneCast(const SpellStats& s,
    const Vector3& muzzle, const Vector3& dir)
{
    int count = (std::max)(1, s.projectileCount);

    if (count == 1 || s.spreadAngle <= 0.0f)
    {
        // 単発：そのまま正面へ
        m_Requests.push_back({ s.id, muzzle, dir,
            s.speed, s.radius, s.damage, s.lifetime });
        return;
    }

    // 分裂：spreadAngle の範囲を count 個で等分し、Y軸まわりに回して扇状に散らす
    //  例) count=3, spread=30 → -15度, 0度, +15度
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
// ============================================================
void WeaponSystem::Update(Registry& reg, float dt, const CollisionSystem& collision)
{
    m_Requests.clear();
    m_Spawned.clear();
    reg.CreateView<TransformComponent, WandComponent>()
        .Each([&](Entity e, TransformComponent& tf, WandComponent& wand)
            {
                // ---- マナ回復（杖で共有）----
                wand.manaCurrent = (std::min)(wand.manaMax, wand.manaCurrent + wand.manaRegen * dt);

                Vector3 muzzle = tf.position + wand.muzzleOffset;

                // ---- 索敵は杖で1回だけ（出力源全員が同じ相手を狙う）----
                Entity target = 0;
                bool hasTarget = collision.FindNearestEntity(muzzle, wand.range, Layer_Enemy, target);

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

                // ---- 出力源ごとに独立処理 ----
                for (auto& s : wand.spells)
                {
                    // === 二重釈放の連射（前回の施法の残り）===
                    // ※こちらを先に処理する。連射中は新しい施法を始めない。
                    if (s.pendingCasts > 0)
                    {
                        s.delayTimer -= dt;
                        if (s.delayTimer <= 0.0f)
                        {
                            if (hasTarget && wand.manaCurrent >= s.manaCost)
                            {
                                QueueOneCast(s, muzzle, aimDir);
                                wand.manaCurrent -= s.manaCost;
                            }
                            --s.pendingCasts;
                            s.delayTimer = s.castDelay;
                        }
                        continue;   // 連射中は新規施法をスキップ
                    }

                    // === 新しい施法 ===
                    s.castTimer -= dt;
                    if (s.castTimer > 0.0f) continue;
                    if (!hasTarget) continue;                       // 敵がいない → 撃たない
                    if (wand.manaCurrent < s.manaCost) continue;    // マナ不足 → 回復待ち

                    // 1回目を発射
                    QueueOneCast(s, muzzle, aimDir);
                    wand.manaCurrent -= s.manaCost;

                    // 二重釈放：残り回数を積んで、次フレーム以降に撃つ
                    s.pendingCasts = (std::max)(0, s.castCount - 1);
                    s.delayTimer = s.castDelay;

                    s.castTimer = s.castInterval;
                }
            });

    // ---- 走査後にまとめて投射物を生成 ----
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
        // ※ Rigidbody は付けない（position の二重書き込み回避）

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