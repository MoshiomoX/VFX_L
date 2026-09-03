// ============================================================
// SpawnDirector.cpp
// ============================================================
#include "Enemy/SpawnDirector.h"
#include "ECS/Registry.h"
#include "ECS/View.h"
#include "Component/TransformComponent.h"
#include "Enemy/EnemyTags.h"
#include "World/GridWorld.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <vector>

using DirectX::SimpleMath::Vector3;

namespace
{
    // XZ 平面の距離の二乗（高さは戦場の意味を持たないので無視する）
    inline float DistSqXZ(const Vector3& a, const Vector3& b)
    {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return dx * dx + dz * dz;
    }
}

void SpawnDirector::Update(Registry& reg, const GridWorld& grid,
    const Vector3& playerPos, float dt, const SpawnFunc& spawn)
{
    if (!enabled || !spawn) return;

    // ---- 1) 現在の雑魚を集める（計数 + 押し出し候補の材料）----
    struct Mob { Entity e; float distSq; };
    std::vector<Mob> mobs;

    reg.CreateView<TransformComponent, MobTag>()
        .Each([&](Entity e, TransformComponent& tf, MobTag&)
            {
                mobs.push_back({ e, DistSqXZ(tf.position, playerPos) });
            });

    m_LastMobCount = (int)mobs.size();

    // ---- 2) 湧きのタイミング ----
    m_Timer += dt;
    if (m_Timer < spawnInterval) return;
    m_Timer = 0.0f;

    // ---- 3) 今回湧かせる数と、そのために空ける枠 ----
    const int want = spawnPerTick;
    const int overflow = (int)mobs.size() + want - spawnCap;

    if (overflow > 0)
    {
        // 玩家から遠い順に overflow 体だけ前へ寄せる（全ソート不要）
        const int n = (std::min)(overflow, (int)mobs.size());

        std::partial_sort(mobs.begin(), mobs.begin() + n, mobs.end(),
            [](const Mob& a, const Mob& b) { return a.distSq > b.distSq; });

        // 一括で消す。走査はもう終わっているので Destroy してよい
        for (int i = 0; i < n; ++i)
        {
            if (reg.IsValid(mobs[i].e))
            {
                reg.Destroy(mobs[i].e);
                ++m_TotalEvicted;
            }
        }
    }

    // ---- 4) 環帯から湧かせる ----
    // 候補点が塞がっていたら数回試す。全部外れたらその1体は諦める
    //（次の tick でまた試すので、湧き損ねは自然に回収される）
    for (int i = 0; i < want; ++i)
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const float ang = (float)rand() / RAND_MAX * 6.2831853f;
            const float r = rMin + (rMax - rMin) * ((float)rand() / RAND_MAX);

            Vector3 pos = playerPos + Vector3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);

            int gx = 0, gz = 0;
            grid.WorldToCell(pos, gx, gz);
            if (!grid.IsWalkable(gx, gz)) continue;

            pos.y = 3.0f;   // 少し上から降らせて着地させる
            spawn(pos);
            ++m_TotalSpawned;
            break;
        }
    }
}