// ============================================================
// SpawnDirector.h
// 泰拉瑞亚式の湧き管理。ゲームループの心臓。
//
// 湧き:
//   spawnInterval ごとに、玩家中心の環帯（rMin~rMax）から
//   ランダム角度で候補点を取り、IsWalkable なら湧かせる。
//
// 押し出し（雑魚だけ）:
//   削除の条件は「数の溢れ」であって距離ではない。
//   MobTag の数が cap 未満なら、どれだけ離れても絶対に消えない。
//   溢れた時だけ、XZ 距離が玩家から最も遠い雑魚を消して枠を空ける。
//   → 逃げ回るほど「遠くの追手」が「顔の横の新手」に置き換わる。
//     逃亡だけで勝つ遊び方はここで封じられている。
//
// EliteTag は計数にも押し出しにも一切関与しない。
//
// 実際の敵の組み立てはシーンの SpawnEnemy に委ねる（コールバック）。
//   どんな Component を積むかは敵の種類の話で、湧きの話ではない。
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include <functional>
#include <SimpleMath.h>

class Registry;
class GridWorld;

class SpawnDirector
{
public:
    // 敵を1体組み立てる関数をシーンから借りる
    using SpawnFunc = std::function<void(const DirectX::SimpleMath::Vector3& pos)>;

    void Update(Registry& reg, const GridWorld& grid,
        const DirectX::SimpleMath::Vector3& playerPos, float dt,
        const SpawnFunc& spawn);

    // --- 調整（ImGui から触る）---
    bool  enabled = true;
    int   spawnCap = 40;       // 雑魚の上限
    float spawnInterval = 1.0f;    // 湧き試行の間隔（秒）
    int   spawnPerTick = 3;        // 1回の試行で湧かせる数
    float rMin = 25.0f;   // 環帯の内径（可視範囲より外）
    float rMax = 35.0f;   // 環帯の外径

    // --- 読み取り（ImGui 表示用）---
    int GetLastMobCount() const { return m_LastMobCount; }
    int GetTotalSpawned() const { return m_TotalSpawned; }
    int GetTotalEvicted() const { return m_TotalEvicted; }

private:
    float m_Timer = 0.0f;
    int   m_LastMobCount = 0;
    int   m_TotalSpawned = 0;
    int   m_TotalEvicted = 0;
};