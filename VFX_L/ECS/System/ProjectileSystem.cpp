// ============================================================
// ProjectileSystem.cpp
// ============================================================
#include "ECS/System/ProjectileSystem.h"
#include "ECS/Registry.h"
#include "Component/Projectile/ProjectileComponent.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Collider/CollisionSystem.h"
#include "ECS/View.h"

void ProjectileSystem::Update(Registry& reg, float dt, const CollisionSystem& collision)
{
    m_HitEvents.clear();
    m_ToDestroy.clear();

    // ---- 1) 飛行 + 寿命 ----
    reg.CreateView<TransformComponent, ProjectileComponent>()
        .Each([&](Entity e, TransformComponent& tf, ProjectileComponent& pj)
            {
                tf.position += pj.velocity * dt;   // 直進
                pj.age += dt;
                if (pj.age >= pj.lifetime)
                    m_ToDestroy.push_back(e);
            });

    // ---- 2) 命中判定：衝突ペアから「投射物 vs それ以外」を拾う ----
    for (const auto& pair : collision.GetPairs())
    {
        Entity proj = 0, target = 0;

        bool aIsProj = reg.Has<ProjectileComponent>(pair.a);
        bool bIsProj = reg.Has<ProjectileComponent>(pair.b);

        if (aIsProj && !bIsProj) { proj = pair.a; target = pair.b; }
        else if (bIsProj && !aIsProj) { proj = pair.b; target = pair.a; }
        else continue;   // 投射物同士 or 投射物以外同士 → 命中ではない

        auto& projTf = reg.Get<TransformComponent>(proj);
        auto& projPj = reg.Get<ProjectileComponent>(proj);

        HitEvent ev;
        ev.projectile = proj;
        ev.target = target;
        ev.position = projTf.position;
        ev.damage = projPj.damage;
        m_HitEvents.push_back(ev);

        m_ToDestroy.push_back(proj);   // 命中した投射物は消滅
    }

    // ---- 3) 消滅処理（走査後にまとめて）----
    for (Entity e : m_ToDestroy)
    {
        if (reg.IsValid(e))
            reg.Destroy(e);
    }
}