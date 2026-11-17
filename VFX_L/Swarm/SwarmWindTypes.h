// ============================================================
// SwarmWindTypes.h
// 局所の風源の表（砂旋・渦・突風）。
// HLSL 側は Shader/Common/WindSources.hlsli（WindSource）。
// ============================================================
#pragma once
#include <cstdint>

namespace Swarm
{
    // ============================================================
    // 局所の風源。
    // SwarmWindSourceCS が毎フレーム生きている砂旋から kMaxWindSources 枠の表に書く（空き = radius 0）。
    // 草（GrassVS）・落ち葉（DebrisUpdateCS）・粒子（ParticleUpdateCS）が WindSources.hlsli の式で世界の風に足す。
    // 一方向へ押す分（pushDir × push、中心で最大・縁で 0）も持てる = 突風（前へ吹く渦）。
    // CPU の風源（SwarmSystem::AddWindBurst）も同じ CS が表に足す。
    // HLSL の WindSource と同じ並び（48B）
    // ============================================================
    constexpr uint32_t kMaxWindSources = 16;
    struct WindSource
    {
        float pos[3];      // 足元の中心
        float radius;      // 届く距離 m（0 = 空き）
        float swirl;       // 芯の縁での接線の速さ m/s（上から見て反時計回り）
        float updraft;     // 芯の近くの上昇の速さ m/s
        float inflow;      // 縁で中心へ向かう速さ m/s
        float fade;        // 0〜1（生まれ / 消え）
        float pushDir[2];  // 押す向き（xz 単位ベクトル。0 = 押さない）
        float push;        // 中心での押す速さ m/s
        float self;        // 1 = プレイヤー自身の突風（マントは受けない）。GPU は見ない
    };
    static_assert(sizeof(WindSource) == 48, "WindSource layout mismatch");
}
