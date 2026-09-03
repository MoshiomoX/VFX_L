// ============================================================
// ChaseAISystem.cpp
// ============================================================
#include "Enemy/ChaseAISystem.h"
#include "ECS/Registry.h"
#include "ECS/View.h"
#include "Component/TransformComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Enemy/ChaseAIComponent.h"
#include "Player/PlayerTag.h"
#include "Player/PlayerStateComponent.h"
#include <cmath>
#include <vector>

using DirectX::SimpleMath::Vector3;

void ChaseAISystem::Update(Registry& reg, float dt)
{
    // ---- 1) 玩家の位置 ----
    // 死んでいる玩家は追わない（死体の周りで団子になるのを防ぐ）
    bool hasTarget = false;
    Vector3 targetPos;

    reg.CreateView<TransformComponent, PlayerTag>()
        .Each([&](Entity e, TransformComponent& tf, PlayerTag&)
            {
                if (reg.Has<PlayerStateComponent>(e) &&
                    reg.Get<PlayerStateComponent>(e).IsDead())
                    return;

                targetPos = tf.position;
                hasTarget = true;
            });

    // ---- 2) 分離用に敵の位置を先に集める ----
    // View の二重走査を避ける（走査中の走査は SparseSet の並びに触って危ない）
    struct EnemyPos { Entity e; Vector3 pos; };
    std::vector<EnemyPos> enemies;

    reg.CreateView<TransformComponent, RigidbodyComponent, ChaseAIComponent>()
        .Each([&](Entity e, TransformComponent& tf, RigidbodyComponent&, ChaseAIComponent&)
            {
                enemies.push_back({ e, tf.position });
            });

    // ---- 3) seek + separation → 速度 ----
    reg.CreateView<TransformComponent, RigidbodyComponent, ChaseAIComponent>()
        .Each([&](Entity e, TransformComponent& tf, RigidbodyComponent& rb, ChaseAIComponent& ai)
            {
                // === seek: 常に玩家へ ===
                Vector3 moveDir(0, 0, 0);
                if (hasTarget)
                {
                    moveDir = targetPos - tf.position;
                    moveDir.y = 0.0f;
                    if (moveDir.LengthSquared() > 1e-6f) moveDir.Normalize();
                }

                // === separation: 近い仲間から離れる ===
                // 総当たりだが敵は数十体なので問題ない
                //（増えたら衝突の空間分割と同じ格子に載せる）
                Vector3 sep(0, 0, 0);
                for (const auto& other : enemies)
                {
                    if (other.e == e) continue;

                    Vector3 away = tf.position - other.pos;
                    away.y = 0.0f;
                    const float dSq = away.LengthSquared();
                    if (dSq > separationRadius * separationRadius) continue;
                    if (dSq < 1e-6f) continue;

                    const float dist = std::sqrt(dSq);
                    sep += away / dist * (1.0f - dist / separationRadius);
                }

                // === 速度の書き込み ===
                Vector3 v = moveDir * ai.moveSpeed + sep * separationPower;

                rb.velocity.x = v.x;
                rb.velocity.z = v.z;
                // velocity.y は触らない

                if (v.LengthSquared() > 0.01f)
                    tf.rotation.y = DirectX::XMConvertToDegrees(std::atan2(v.x, v.z));
            });
}