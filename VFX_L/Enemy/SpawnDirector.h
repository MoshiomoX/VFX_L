#pragma once
#include <functional>
#include <SimpleMath.h>

class GridWorld;

class SpawnDirector
{
public:
    using SpawnFunc = std::function<void(const DirectX::SimpleMath::Vector3& pos)>;

    // aliveMobs: GPU 側の生存数（リードバックなので 1〜2 フレーム古い）
    // spawn:     空き枠へ新規に湧かせる
    // recycle:   枠が無い分。遠い雑魚をこの位置へ転送する（GPU が選ぶ）
    void Update(const GridWorld& grid,
        const DirectX::SimpleMath::Vector3& playerPos, float dt,
        int aliveMobs,
        const SpawnFunc& spawn, const SpawnFunc& recycle);

    bool  enabled = true;
    int   spawnCap = 550;           // 同時に居られる雑魚の上限（Megabonk の通常面と同じ）
    float spawnPerSecond = 1.0f;    // 1 秒あたりに湧かせる数（端数は次へ持ち越す）。難度が毎フレーム書く
    int   maxPerFrame = 64;         // 1 フレームで出す上限（止まった後に溜まった分が一度に出ないよう）
    float rMin = 25.0f;
    float rMax = 35.0f;

    int GetLastMobCount() const { return m_LastMobCount; }
    int GetTotalSpawned() const { return m_TotalSpawned; }
    int GetTotalRecycled() const { return m_TotalRecycled; }

private:
    float m_Credit = 0.0f;   // 湧かせる分の端数の貯め
    int   m_LastMobCount = 0;
    int   m_TotalSpawned = 0;
    int   m_TotalRecycled = 0;
};