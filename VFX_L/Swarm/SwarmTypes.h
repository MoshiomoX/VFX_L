// ============================================================
// SwarmTypes.h
// Shader/Common/SwarmCommon.hlsli と一対一で対応する定義。
//
// StructuredBuffer には反射が無い。ズレても警告は出ず、
// ただ結果が壊れるだけなので、必ず両方を同時に直すこと。
// static_assert でサイズだけは機械的に守る。
//
// ※state（生死）は本体の構造体に入れず、別の RWBuffer<uint> に置く。
//   SM5.0 の原子操作は RWByteAddressBuffer と
//   RWBuffer<uint> / RWStructuredBuffer<uint> にしか使えない。
//   スロットの取り合いに InterlockedCompareExchange が要るので、
//   生死だけは独立した uint 配列にする必要がある。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>

namespace Swarm
{
    using DirectX::SimpleMath::Vector3;

    // 固定ステップ（Unity の FixedUpdate 既定値に合わせた）
    constexpr float kFixedStep = 0.02f;
    constexpr int   kMaxSubSteps = 4;   // コマ落ち時の死のスパイラル防止

    // プール容量。溢れたら生成しない（粒子の deadCount 護欄と同じ思想）
    constexpr uint32_t kMaxEnemies = 4096;
    constexpr uint32_t kMaxProjectiles = 8192;
    constexpr uint32_t kMaxOrbs = 4096;

    // 1フレームに受け付ける生成依頼の上限
    constexpr uint32_t kMaxSpawnEnemyPerFrame = 256;
    constexpr uint32_t kMaxSpawnProjPerFrame = 512;

    constexpr uint32_t kStateDead = 0;
    constexpr uint32_t kStateAlive = 1;

    // ============================================================
    // 本体データ（生死は別バッファ）
    // ============================================================
    struct Enemy
    {
        Vector3  position;
        float    hp = 0.0f;
        Vector3  velocity;
        float    moveSpeed = 3.5f;
        float    yaw = 0.0f;

        // ---- 将来のアニメーション用（今は誰も読まない）----
        // VAT（頂点アニメーションテクスチャ）方式を想定。
        // CS が animTime を進め、VS がテクスチャから引く形なら、
        // 骨の計算そのものが実行時に存在しなくなる
        float    animTime = 0.0f;
        uint32_t animIndex = 0;   // 0=待機 1=歩行 2=被弾
        float    _pad = 0.0f;
    };
    static_assert(sizeof(Enemy) == 48, "SwarmEnemy layout mismatch");

    struct Projectile
    {
        Vector3  position;
        float    damage = 0.0f;
        Vector3  velocity;
        float    lifetime = 0.0f;
        float    radius = 0.25f;
        uint32_t vfxType = 0;      // Recipe 表の index（VFXId から引く）
        float    _pad[2] = {};
    };
    static_assert(sizeof(Projectile) == 48, "SwarmProjectile layout mismatch");
    
    struct Orb
    {
        Vector3  position;
        float    amount = 0.0f;
        Vector3  velocity;
        float    _pad = 0.0f;
    };
    static_assert(sizeof(Orb) == 32, "SwarmOrb layout mismatch");

    // ============================================================
    // 毎フレームの定数（b0）
    // ============================================================
    struct FrameCB
    {
        Vector3  playerPos;
        float    playerRadius = 0.4f;

        float    step = kFixedStep;
        uint32_t maxEnemies = kMaxEnemies;
        uint32_t maxProjectiles = kMaxProjectiles;
        uint32_t maxOrbs = kMaxOrbs;

        Vector3  gridOrigin;
        float    cellSize = 2.0f;

        uint32_t gridW = 0;
        uint32_t gridD = 0;
        uint32_t seed = 0;
        uint32_t playerAlive = 1;
    };
    static_assert(sizeof(FrameCB) == 64, "SwarmFrameCB layout mismatch");

    // ============================================================
    // 生成用の定数（b1）
    // ============================================================
    struct SpawnCB
    {
        uint32_t requestCount = 0;
        uint32_t scanStart = 0;    // 毎フレーム回して探索開始位置をずらす
        uint32_t _pad[2] = {};
    };
    static_assert(sizeof(SpawnCB) == 16, "SwarmSpawnCB layout mismatch");
}