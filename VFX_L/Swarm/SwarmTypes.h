// ============================================================
// SwarmTypes.h
// GPU 群体（雑魚・弾・範囲・経験値オーブ）の C++ 側の並びの入口。中身は主題ごとのヘッダにあり、
// Shader/Common/SwarmCommon.hlsli が include する各ヘッダ（SwarmCore / SwarmEnemyTypes /
// SwarmProjectileTypes / SwarmAreaTypes / SwarmOrbTypes / SwarmFrameCB / SwarmKindCB / SwarmOrbCB など）と
// 一対一で対応する。
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
#include "Swarm/SwarmCoreTypes.h"         // 固定ステップ・上限・SwarmCounters（SwarmCore.hlsli）
#include "Swarm/SwarmEnemyTypes.h"        // 雑魚・種類・描画リスト・砕け散り（SwarmEnemyTypes.hlsli）
#include "Swarm/SwarmProjectileTypes.h"   // 弾・運動表・誘発（SwarmProjectileTypes.hlsli）
#include "Swarm/SwarmAreaTypes.h"         // 範囲攻撃（SwarmAreaTypes.hlsli）
#include "Swarm/SwarmOrbTypes.h"          // 経験値オーブ（SwarmOrbTypes.hlsli / SwarmOrbCB.hlsli）
#include "Swarm/SwarmCBs.h"               // 毎フレーム・生成・AI・転送・光線の CB（SwarmFrameCB.hlsli など）
#include "Swarm/SwarmKindCB.h"            // 敵の種類ごとの CB（SwarmKindCB.hlsli）
#include "Swarm/SwarmReadbackTypes.h"     // GPU → CPU のリードバックの並び
#include "Swarm/SwarmWindTypes.h"         // 局所の風源（WindSources.hlsli）
