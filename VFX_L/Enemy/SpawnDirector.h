#pragma once
#include <functional>
#include <SimpleMath.h>

class GridWorld;

class SpawnDirector
{
public:
    using SpawnFunc = std::function<void(const DirectX::SimpleMath::Vector3& pos)>;

    // aliveMobs: GPU 側の存活数（回読なので 1〜2 フレーム古い）
    // spawn:     空き枠へ新規に湧かせる
    // recycle:   枠が無い分。遠い雑魚をこの位置へ転送する（GPU が選ぶ）
    void Update(const GridWorld& grid,
        const DirectX::SimpleMath::Vector3& playerPos, float dt,
        int aliveMobs,
        const SpawnFunc& spawn, const SpawnFunc& recycle);

    bool  enabled = true;
    int   spawnCap = 40;
    float spawnInterval = 2.0f;
    int   spawnPerTick = 1;
    float rMin = 25.0f;
    float rMax = 35.0f;

    int GetLastMobCount() const { return m_LastMobCount; }
    int GetTotalSpawned() const { return m_TotalSpawned; }
    int GetTotalRecycled() const { return m_TotalRecycled; }

private:
    float m_Timer = 0.0f;
    int   m_LastMobCount = 0;
    int   m_TotalSpawned = 0;
    int   m_TotalRecycled = 0;
};