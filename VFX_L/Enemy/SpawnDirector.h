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
    // 湧く環（プレイヤーからの距離 m）。2026-10-07 ユーザー：範囲を 1.5 倍に（25〜35 → 37.5〜52.5）。
    // GPU の転送（RecycleCS）の「遠い雑魚」の閾値も rMax に追従する（MobSpawner::Update）
    float rMin = 37.5f;
    float rMax = 52.5f;
    // 湧く方向（2026-10-07、湧きの波）：arcYaw（ラジアン、x = cos・z = sin）の ± arcHalf だけ。π = 全周。
    // 方向の中が歩けない所ばかりの時は、最後の数回は全周から探す（湧き損ねないように）
    float arcYaw = 0.0f;
    float arcHalf = 3.14159265f;

    int GetLastMobCount() const { return m_LastMobCount; }
    int GetTotalSpawned() const { return m_TotalSpawned; }
    int GetTotalRecycled() const { return m_TotalRecycled; }

private:
    float m_Credit = 0.0f;   // 湧かせる分の端数の貯め
    int   m_LastMobCount = 0;
    int   m_TotalSpawned = 0;
    int   m_TotalRecycled = 0;
};