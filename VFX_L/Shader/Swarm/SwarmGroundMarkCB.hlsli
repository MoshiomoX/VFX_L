// ============================================================
// SwarmGroundMarkCB.hlsli
// 地面に出す予告の印の cbuffer（b0）。自爆兵の円（SwarmBomberRingVS / PS）、
// 警告の円（SwarmWarnRingVS）、メテオの落下円（SwarmDropRingVS）、突進の帯
// （SwarmChargeLineVS / PS）で共用する。C++ 側：SwarmSystem::GroundMarkCB
// ============================================================
#ifndef SWARM_GROUND_MARK_CB_HLSLI
#define SWARM_GROUND_MARK_CB_HLSLI

cbuffer SwarmGroundMarkCB : register(b0)
{
    row_major float4x4 g_RingView;
    row_major float4x4 g_RingProj;
    float4 g_RingFill;     // 広がる円盤 / 伸びる帯の塗り（ストレートアルファ）
    float4 g_RingEdge;     // 縁
    float4 g_RingBack;     // 縁の内側で、まだ塗られていない所
    float g_RingEdgeWidth; // 縁の太さ（帯では横の縁）、m
    float g_RingLift;      // 地面からの高さ、m
    float2 _ringPad;
};

#endif
