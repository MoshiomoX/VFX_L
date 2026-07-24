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

using DirectX::SimpleMath::Vector3;

void WeaponSystem::Update(Registry& reg, float dt, const CollisionSystem& collision)
{
    m_Requests.clear();

    // ---- 1) 杖を走査：マナ回復・発射判定（生成はまだしない）----
    reg.CreateView<TransformComponent, WandComponent>()
        .Each([&](Entity e, TransformComponent& tf, WandComponent& wand)
            {
                // マナ回復
                wand.manaCurrent = min(wand.manaMax, wand.manaCurrent + wand.manaRegen * dt);

                // 発射間隔
                wand.castTimer -= dt;
                if (wand.castTimer > 0.0f) return;

                // マナ不足 → 今回は撃たない（タイマーは進んだままなので回復し次第すぐ撃つ）
                if (wand.manaCurrent < wand.manaCost) return;

                // 索敵：自分を中心に全方位、最も近い敵
                Vector3 muzzle = tf.position + wand.muzzleOffset;
                Entity target = 0;
                if (!collision.FindNearestEntity(muzzle, wand.range, Layer_Enemy, target))
                    return;   // 敵がいなければ撃たない（マナも消費しない）

                // 敵の中心を狙う（collider の offset を考慮）
                Vector3 targetPos = reg.Get<TransformComponent>(target).position;
                if (reg.Has<ColliderComponent>(target))
                    targetPos += reg.Get<ColliderComponent>(target).offset;

                Vector3 dir = targetPos - muzzle;
                if (dir.LengthSquared() < 1e-6f) return;
                dir.Normalize();

                m_Requests.push_back({ muzzle, dir,
                    wand.projectileSpeed, wand.projectileRadius,
                    wand.projectileDamage, wand.projectileLifetime });

                wand.manaCurrent -= wand.manaCost;
                wand.castTimer = wand.castInterval;
            });

    // ---- 2) 走査が終わってから投射物を生成する ----
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
        col.mask = Layer_Enemy | Layer_Terrain;   // 敵と地形にのみ当たる
        reg.Add<ColliderComponent>(p, col);

        ProjectileComponent pj;
        pj.velocity = req.dir * req.speed;
        pj.damage = req.damage;
        pj.lifetime = req.lifetime;
        reg.Add<ProjectileComponent>(p, pj);

        // ※ RigidbodyComponent は付けない
        //   （PhysicsSystem と ProjectileSystem が両方 position を書いて競合するため）

        if (m_ProjectileModel)
        {
            ModelComponent mc;
            mc.model = m_ProjectileModel;
            reg.Add<ModelComponent>(p, mc);
        }
    }
}