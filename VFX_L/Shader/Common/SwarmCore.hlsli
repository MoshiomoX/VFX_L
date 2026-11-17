// ============================================================
// SwarmCore.hlsli
// Swarm の共通ヘッダの土台：固定ステップ、スロットの生死の値、カウンタのオフセット、
// HP と大きさの固定小数。
//
// C++ 側（Swarm/SwarmTypes.h）とレイアウトを完全に一致させること。
// StructuredBuffer にはリフレクションがないので、ずれても黙って壊れる。
//
// レイアウトの規則：どの構造体も 16 バイトの倍数にし、
// float3 が 16 バイト境界をまたがないようにする。
//
// 注意：生死フラグはこれらの構造体に入っていない。別の
// RWBuffer<uint> に置いている。SM5.0 のアトミックは
// RWByteAddressBuffer と RWBuffer<uint> / RWStructuredBuffer<uint> でしか使えず、
// スロットの確保に InterlockedCompareExchange が要るため。
// ============================================================

#ifndef SWARM_CORE_HLSLI
#define SWARM_CORE_HLSLI

// ---- 固定ステップ（このパイプラインの全 CS で共通） ----
static const float SWARM_FIXED_STEP = 0.02; // Unity のデフォルト（50Hz）

// ---- スロットの状態（別の状態バッファに入る） ----
static const uint SWARM_DEAD = 0;
static const uint SWARM_ALIVE = 1;

// ---- カウンタのバイトオフセット（RWByteAddressBuffer） ----
static const uint SWARM_CNT_ALIVE_ENEMIES = 0;
static const uint SWARM_CNT_KILLS = 4;
static const uint SWARM_CNT_PLAYER_DAMAGE = 8;
static const uint SWARM_CNT_ALIVE_PROJ = 12;

// 杖の狙い（優先度付きの距離 SwarmAimKeyDist で一番小さい敵）。key = (asuint(dist) & 0xFFFFF000) | slot。
// 正の float は uint として比べても大小が同じなので、InterlockedMin 1 回で
// 最短距離とその敵を同時に求められる。下位 12 ビット = 4096 スロット。kMaxEnemies
// は 4096 以下に保つこと（超えるとこの符号化が壊れる）。POS / VEL / DIST は AimResolveCS が書く
static const uint SWARM_CNT_AIM_KEY = 16;
static const uint SWARM_CNT_AIM_POS = 20;
static const uint SWARM_CNT_AIM_VEL = 32;
static const uint SWARM_CNT_AIM_DIST = 44;
// プレイヤーが拾った経験値、固定小数 x100。KILLS / PLAYER_DAMAGE と同じく
// 永久に累積し、CPU が差分を取る。拾った時に
// OrbMoveCS が書く。
static const uint SWARM_CNT_EXP = 48;
// 生きている経験玉の数。毎ステップクリア（ALIVE_ENEMIES と同じ）
static const uint SWARM_CNT_ALIVE_ORBS = 52;
// 範囲攻撃。どちらも毎ステップクリア（ALIVE_ENEMIES と同じ）
static const uint SWARM_CNT_ALIVE_AREAS = 56;
static const uint SWARM_CNT_TICKING_AREAS = 60; // このステップでダメージを与える範囲
// 合計 64 バイト
static const uint SWARM_NO_TARGET_KEY = 0xFFFFFFFFu;
static const uint SWARM_SLOT_MASK = 0xFFFu;
static const uint SWARM_DIST_MASK = 0xFFFFF000u;

// 優先度なしの一番近い敵（nearestTarget の RWByteAddressBuffer、32B）のバイトオフセット。Swarm::NearestTarget と一致させること
#define SWARM_NEAREST_KEY 0u              // 距離 | スロット（MoveCS が InterlockedMin）
#define SWARM_NEAREST_POS 4u              // float3 位置（AimResolveCS）
#define SWARM_NEAREST_DIST 16u            // 水平距離、1e30 = 居ない（AimResolveCS）
#define SWARM_NEAREST_CROWD_COUNTING 20u  // このステップで数えている 10m 以内の敵の数（MoveCS）
#define SWARM_NEAREST_CROWD 24u           // 前のステップの数（CPU が読む）

#define SWARM_HP_SCALE 100.0
uint SwarmHpToFixed(float hp)
{
    return (uint) (hp * SWARM_HP_SCALE + 0.5);
}

// ------------------------------------------------------------
// 弾 / 範囲 1 つの大きさの倍率（拡大鏡のアイテム）。
// 弾の倍率 = 当たり判定の半径 / プロファイルの半径
// （SwarmMotion.baseRadius）。SwarmSpawnProjCS で一度だけ求め、
// SwarmProjectile.motion のビット 16-30 に入れる（読み手はどれも添字を
// SWARM_MOTION_INDEX_MASK でマスクする）。範囲は倍率を SwarmArea.flags の
// ビット 16-31 に持つ。固定小数、1024 = 1.0、0 = 1.0（CPU からの生成
// リクエストはこのビットを空のままにする）。
// 掛かる先：命中範囲の半径、パーティクルの大きさ / オフセット /
// 形状 / 速度（SwarmEmitCS）、点光源の半径、スプライトの大きさ、
// メテオの警告リング。
// ------------------------------------------------------------
static const uint SWARM_SCALE_SHIFT = 16u;
static const float SWARM_SCALE_ONE = 1024.0;

uint SwarmPackScale(float scale, uint maxQ)
{
    return (uint) clamp(round(scale * SWARM_SCALE_ONE), 1.0, (float) maxQ);
}
float SwarmUnpackScale(uint q)
{
    return (q == 0u) ? 1.0 : (float) q / SWARM_SCALE_ONE;
}

#endif
