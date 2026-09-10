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
#include "World/GridWorld.h"
#include <cmath>
#include <vector>

using DirectX::SimpleMath::Vector3;

void ChaseAISystem::Update(Registry& reg, const GridWorld& grid, float dt)
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
    struct EnemyPos { Entity e; Vector3 pos; };
    std::vector<EnemyPos> enemies;

    reg.CreateView<TransformComponent, RigidbodyComponent, ChaseAIComponent>()
        .Each([&](Entity e, TransformComponent& tf, RigidbodyComponent&, ChaseAIComponent&)
            {
                enemies.push_back({ e, tf.position });
            });

    const float cs = GridWorld::kCellSize;

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
                // 数体分で押し合いの方向は決まるので早退する
                Vector3 sep(0, 0, 0);
                int found = 0;
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

                    if (++found >= 6) break;
                }

                // === avoid: 塞がったマスから離れる（軟）===
                // 上下左右の4マスだけ見る。斜めまで見ても方向はほぼ変わらない
                Vector3 avoid(0, 0, 0);
                {
                    const Vector3& p = tf.position;
                    if (!grid.IsWalkableAt({ p.x + cs, p.y, p.z })) avoid.x -= 1.0f;
                    if (!grid.IsWalkableAt({ p.x - cs, p.y, p.z })) avoid.x += 1.0f;
                    if (!grid.IsWalkableAt({ p.x, p.y, p.z + cs })) avoid.z -= 1.0f;
                    if (!grid.IsWalkableAt({ p.x, p.y, p.z - cs })) avoid.z += 1.0f;
                }

                // === 合成 ===
                Vector3 v = moveDir * ai.moveSpeed
                    + sep * separationPower
                    + avoid * avoidPower;

                // === 進入阻止（硬）===
                // 行き先のマスが塞がっていれば、その軸の速度を殺す。
                // 軸ごとに判定するので、壁に沿って滑る動きが自然に出る。
                //
                // ※今いるマスが塞がっている時は素通しする。
                //   何かの拍子に壁の中に入ってしまった敵を
                //   永久に閉じ込めないため（出る方向も塞がれてしまう）
                const Vector3 cur = tf.position;
                if (grid.IsWalkableAt(cur))
                {
                    const float ax = v.x * lookAhead;
                    const float az = v.z * lookAhead;

                    if (ax != 0.0f && !grid.IsWalkableAt({ cur.x + ax, cur.y, cur.z }))
                        v.x = 0.0f;
                    if (az != 0.0f && !grid.IsWalkableAt({ cur.x, cur.y, cur.z + az }))
                        v.z = 0.0f;
                }

                // === 速度と位置 ===
                rb.velocity.x = v.x;
                rb.velocity.z = v.z;

                // 雑魚は PhysicsSystem を通らないので、自分で位置を進める
                tf.position.x += v.x * dt;
                tf.position.z += v.z * dt;

                // 重力も落下も無い。地面は平らなので高さを固定するだけ
                tf.position.y = groundY;
                rb.velocity.y = 0.0f;

                if (v.LengthSquared() > 0.01f)
                    tf.rotation.y = DirectX::XMConvertToDegrees(std::atan2(v.x, v.z));
            });
}