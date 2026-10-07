// ============================================================
// SpawnDirector.cpp
// ============================================================
#include "Enemy/SpawnDirector.h"
#include "ECS/Registry.h"
#include "ECS/View.h"
#include "Component/TransformComponent.h"
#include "Enemy/EnemyTags.h"
#include "World/GridWorld.h"

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

void SpawnDirector::Update(const GridWorld& grid,
    const Vector3& playerPos, float dt, int aliveMobs,
    const SpawnFunc& spawn, const SpawnFunc& recycle)
{
    if (!enabled || !spawn || !recycle) return;

    m_LastMobCount = aliveMobs;

    // 1 秒あたり spawnPerSecond 体。端数は貯めて次のフレームへ
    m_Credit += (std::max)(0.0f, spawnPerSecond) * dt;
    const int want = (std::min)((int)m_Credit, maxPerFrame);
    if (want <= 0) return;
    m_Credit = (std::min)(m_Credit - (float)want, (float)maxPerFrame);

    // ---- 今回の内訳：空き枠に入る分は新規、残りは転送 ----
    // 押し出し（Destroy）はもうしない。溢れた分は GPU が
    // 「プレイヤーから rMax より遠い雑魚」を選んで湧き位置へ転送する
    const int freeSlots = (std::max)(0, spawnCap - aliveMobs);
    const int newCount = (std::min)(want, freeSlots);

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
            // y = その場所の地面の高さ（山頂・洞窟の底。湧いた所から落ちたり埋まったりしない）
            pos.y = grid.SampleHeight(pos.x, pos.z);

            if (i < newCount) { spawn(pos);   ++m_TotalSpawned; }
            else { recycle(pos); ++m_TotalRecycled; }
            break;
        }
    }
}