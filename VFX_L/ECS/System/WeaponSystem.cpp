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
// 1???????????????????????(????)
// ============================================================
void WeaponSystem::QueueOneCast(const SpellStats& s,
    const Vector3& muzzle, const Vector3& dir)
{
    int count = (std::max)(1, s.projectileCount);

    if (count == 1 || s.spreadAngle <= 0.0f)
    {
        // ??:???????
        m_Requests.push_back({ s.id, muzzle, dir,
            s.speed, s.radius, s.damage, s.lifetime });
        return;
    }

    // ??:spreadAngle ???? count ??????Y??????????????
    //  ?) count=3, spread=30 ? -15?, 0?, +15?
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
                // ---- ????(????)----
                wand.manaCurrent = (std::min)(wand.manaMax,
                    wand.manaCurrent + wand.manaRegen * dt);

                // ---- ????????????? ----
                if (wand.castAnimTimer > 0.0f)
                    wand.castAnimTimer -= dt;

                Vector3 muzzle = tf.position + wand.muzzleOffset;

                // ---- ?????1???(?????????????)----
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

                // ---- ????????? ----
                // ?pendingCasts ???????????
                //   ???1???????????????????(1?? = 1 combo)?
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

                // ---- ?????????? ----
                for (auto& s : wand.spells)
                {
                    // === ???????(????????)===
                    if (s.pendingCasts > 0)
                    {
                        s.delayTimer -= dt;
                        if (s.delayTimer <= 0.0f)
                        {
                            if (hasTarget && wand.manaCurrent >= s.manaCost)
                            {
                                QueueOneCast(s, muzzle, aimDir);
                                wand.manaCurrent -= s.manaCost;
                                wand.castAnimTimer = wand.castAnimDuration;
                            }
                            --s.pendingCasts;
                            s.delayTimer = s.castDelay;
                        }
                        continue;   // ?????????????
                    }

                    // === ????? ===
                    // ?castTimer ?????????????????????
                    //   ????????????????????????
                    s.castTimer -= dt;
                    if (!ignoreCooldown && s.castTimer > 0.0f) continue;
                    if (!allowNewCast) continue;
                    if (!hasTarget) continue;
                    if (wand.manaCurrent < s.manaCost) continue;

                    QueueOneCast(s, muzzle, aimDir);
                    wand.manaCurrent -= s.manaCost;
                    wand.castAnimTimer = wand.castAnimDuration;

                    // ????:???????????????????
                    s.pendingCasts = (std::max)(0, s.castCount - 1);
                    s.delayTimer = s.castDelay;
                    s.castTimer = s.castInterval;
                }
            });

    // ---- ?????????????? ----
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
        // ? Rigidbody ?????(position ?????????)

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