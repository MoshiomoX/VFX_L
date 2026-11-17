// ============================================================
// SwarmCoreTypes.h
// 固定ステップ・プールの上限・HP の固定小数・生死の値と、GPU の計数器 SwarmCounters。
// HLSL 側は Shader/Common/SwarmCore.hlsli（SWARM_CNT_* のオフセット）。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstddef>
#include <cstdint>

namespace Swarm
{
    using DirectX::SimpleMath::Vector3;

    // 固定ステップ（Unity の FixedUpdate 既定値に合わせた）
    constexpr float kFixedStep = 0.02f;
    constexpr int   kMaxSubSteps = 4;   // コマ落ち時の死のスパイラル防止

    // ---- 経路探索（FlowField）の段差の既定値。GPU の判定と揃えておく ----
    // 隣のマスとの中心の高さの差がこれを超えたら崖（2m マスで 1.5m ≒ 37°）。HLSL の kFlowLevelStep（SwarmEnemyAICS）と同じ
    constexpr float kFlowMaxStep = 1.5f;
    // 境の段差の上限 = HLSL の SWARM_BODY_MAX_SLOPE（0.62）× 半径 0.4 + SWARM_BODY_CLIFF_RISE（0.35）≒ 0.6
    constexpr float kFlowEdgeStepMax = 0.6f;
    // 下りの崖を飛び降りてよいのは目標よりこれ以上高い時。HLSL の SWARM_DROP_MIN_BELOW と同じ
    constexpr float kFlowDropMinBelow = 1.0f;

    // プール容量。溢れたら生成しない（粒子の deadCount ガードと同じ思想）
    constexpr uint32_t kMaxEnemies = 4096;
    // 狙いの鍵（SWARM_CNT_AIM_KEY など）は下位 12 bit にスロットを入れる（SWARM_SLOT_MASK）
    static_assert(kMaxEnemies <= 4096, "kMaxEnemies must fit in the 12-bit slot of the aim key (SWARM_SLOT_MASK)");
    constexpr uint32_t kMaxProjectiles = 8192;
    constexpr uint32_t kMaxOrbs = 4096;

    // 1フレームに受け付ける生成依頼の上限
    constexpr uint32_t kMaxSpawnEnemyPerFrame = 256;
    constexpr uint32_t kMaxSpawnProjPerFrame = 512;

    // 範囲攻撃。HLSL の SWARM_MAX_AREAS と一致させること
    constexpr uint32_t kMaxAreas = 256;
    constexpr uint32_t kMaxAreaDefs = 64;          // 0 番 = 無し
    constexpr uint32_t kMaxSpawnAreaPerFrame = 64;

    constexpr float kHpScale = 100.0f;
    inline uint32_t HpToFixed(float hp) { return (uint32_t)(hp * kHpScale + 0.5f); }
    inline float    HpFromFixed(uint32_t h) { return (float)h / kHpScale; }

    constexpr uint32_t kStateDead = 0;
    constexpr uint32_t kStateAlive = 1;
}

// ============================================================
// GPU の計数器。CS が RWByteAddressBuffer に手で書き、CPU が StagingRing で 3 枚輪転してリードバックする。
// killCount / playerDamage / expTotal は GPU 上で永久に累積し、CPU は前回値との差分を取る
// ============================================================
struct SwarmCounters
{
    uint32_t aliveEnemies = 0;      // 0
    uint32_t killCount = 0;         // 4
    uint32_t playerDamage = 0;      // 8
    uint32_t aliveProjectiles = 0;  // 12

    // ---- 杖の狙い（優先度付き。雑魚が近くに居なければ Boss / エリートが先。SwarmAimKeyDist）----
    // aimKey は GPU 内部用（優先度付きの距離の上位 20bit | スロット 12bit）。CPU は読まない。aimDist は実際の水平距離
    uint32_t aimKey = 0xFFFFFFFFu;  // 16
    float    aimPos[3] = {};        // 20
    float    aimVel[3] = {};        // 32
    float    aimDist = 1e30f;       // 44  目標無しなら巨大値

    // ---- 経験値 ----
    // expTotal は固定小数 ×100、GPU 上で永久に累積（kills と同じ扱い。CPU は差分を取る）
    uint32_t expTotal = 0;              // 48
    uint32_t aliveOrbs = 0;             // 52  毎ステップ再計算
    // ---- 範囲攻撃（どちらも毎ステップ再計算）----
    uint32_t aliveAreas = 0;            // 56
    uint32_t tickingAreas = 0;          // 60  そのステップでダメージを出した範囲の数
};
static_assert(sizeof(SwarmCounters) == 64, "SwarmCounters layout mismatch");
// HLSL は RWByteAddressBuffer に手で書く。オフセットは SwarmCore.hlsli の SWARM_CNT_* と一致させること
static_assert(offsetof(SwarmCounters, aliveEnemies) == 0, "SwarmCounters: SWARM_CNT_ALIVE_ENEMIES");
static_assert(offsetof(SwarmCounters, killCount) == 4, "SwarmCounters: SWARM_CNT_KILLS");
static_assert(offsetof(SwarmCounters, playerDamage) == 8, "SwarmCounters: SWARM_CNT_PLAYER_DAMAGE");
static_assert(offsetof(SwarmCounters, aliveProjectiles) == 12, "SwarmCounters: SWARM_CNT_ALIVE_PROJ");
static_assert(offsetof(SwarmCounters, aimKey) == 16, "SwarmCounters: SWARM_CNT_AIM_KEY");
static_assert(offsetof(SwarmCounters, aimPos) == 20, "SwarmCounters: SWARM_CNT_AIM_POS");
static_assert(offsetof(SwarmCounters, aimVel) == 32, "SwarmCounters: SWARM_CNT_AIM_VEL");
static_assert(offsetof(SwarmCounters, aimDist) == 44, "SwarmCounters: SWARM_CNT_AIM_DIST");
static_assert(offsetof(SwarmCounters, expTotal) == 48, "SwarmCounters: SWARM_CNT_EXP");
static_assert(offsetof(SwarmCounters, aliveOrbs) == 52, "SwarmCounters: SWARM_CNT_ALIVE_ORBS");
static_assert(offsetof(SwarmCounters, aliveAreas) == 56, "SwarmCounters: SWARM_CNT_ALIVE_AREAS");
static_assert(offsetof(SwarmCounters, tickingAreas) == 60, "SwarmCounters: SWARM_CNT_TICKING_AREAS");
