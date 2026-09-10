// ============================================================
// ContactDamageSystem.cpp
// ============================================================
#include "Enemy/ContactDamageSystem.h"
#include "ECS/Registry.h"
#include "ECS/View.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Enemy/ChaseAIComponent.h"
#include "Player/PlayerTag.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerStateSystem.h"

using DirectX::SimpleMath::Vector3;

void ContactDamageSystem::Update(Registry& reg, float dt)
{
    m_LastTouchCount = 0;

    // ---- 玩家を1体だけ拾う ----
    Entity player = 0;
    bool found = false;
    float playerRadius = 0.4f;
    Vector3 playerPos;

    reg.CreateView<TransformComponent, PlayerTag>()
        .Each([&](Entity e, TransformComponent& tf, PlayerTag&)
            {
                // 死んでいるなら判定しない（死体を殴り続けない）
                if (reg.Has<PlayerStateComponent>(e) &&
                    reg.Get<PlayerStateComponent>(e).IsDead())
                    return;

                player = e;
                playerPos = tf.position;
                if (reg.Has<ColliderComponent>(e))
                    playerRadius = reg.Get<ColliderComponent>(e).radius;
                found = true;
            });

    if (!found) return;

    // ---- 敵との XZ 距離を比べるだけ ----
    // 高さは戦場の意味を持たないので無視する（他の System と同じ扱い）
    const float touchDist = playerRadius + reach;
    const float touchSq = touchDist * touchDist;

    reg.CreateView<TransformComponent, ChaseAIComponent>()
        .Each([&](Entity e, TransformComponent& tf, ChaseAIComponent&)
            {
                const float dx = tf.position.x - playerPos.x;
                const float dz = tf.position.z - playerPos.z;
                if (dx * dx + dz * dz > touchSq) return;

                ++m_LastTouchCount;

                // 無敵中なら false が返るだけ。連打の抑制は向こうの担当
                PlayerStateSystem::TryApplyHit(reg, player, damage);
            });
}