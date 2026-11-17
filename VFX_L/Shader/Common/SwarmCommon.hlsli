// SwarmCommon.hlsli
// Swarm の共通ヘッダの入口。中身は下の各ファイル（順番に依存があるので並びを変えない）。
#ifndef SWARM_COMMON_HLSLI
#define SWARM_COMMON_HLSLI
#include "SwarmCore.hlsli"
#include "SwarmEnemyTypes.hlsli"
#include "SwarmProjectileTypes.hlsli"
#include "SwarmAreaTypes.hlsli"
#ifdef SWARM_AREA_POOL_U
#include "SwarmAreaSpawn.hlsli"
#endif
#include "SwarmOrbTypes.hlsli"
#include "SwarmRecipe.hlsli"
#include "SwarmFrameCB.hlsli"
#ifdef SWARM_KIND_CB_REG
#include "SwarmKindCB.hlsli"
#endif
#include "SwarmQuat.hlsli"
#include "SwarmOrbCB.hlsli"
#include "SwarmTerrain.hlsli"

// 敵の出来事の環（GPU -> CPU。SwarmEnemyAICS と SwarmCorpseTrackCS が書く）の枠数。
// +0 = これまでの総数、続いて 16 バイト × これ（float3 位置 + uint 種類 | 中身 << 16）。
// Swarm::kMaxEnemyEvents と一致させること
static const uint SWARM_MAX_ENEMY_EVENTS = 128u;
#endif
