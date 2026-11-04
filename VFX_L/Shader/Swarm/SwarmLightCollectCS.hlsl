// ============================================================
// SwarmLightCollectCS.hlsl
// GPU の弾 1 つにつき 1 スレッド。生きていてレシピがライトを持つなら、
// 弾の位置に PointLight を 1 つ、このフレームのライト一覧（PointLights.hlsli）
// に追加する。一覧には CPU 側のライトが既に入っていて、カウンタは
// その続きから数える。
//
// 一覧には上限がある（MAX_POINT_LIGHTS。2026-10-23 のクラスタ化以降 256）。
// スロットは早い者勝ち -- 火球が数百あっても
// 光るのは先頭の分だけ。
//
// SwarmAreaLightCollectCS.hlsl は SWARM_LIGHT_AREAS を #define してこの
// ファイルを include する: コードは同じで、光源が生きている範囲になる。
// ============================================================
#include "../Common/SwarmCommon.hlsli"
#include "../Common/PointLights.hlsli"   // struct PointLight（SRV はここでは使わない）

// SwarmVFXTable.h の Swarm::VFXLightEntry（32 バイト）と一致させること
struct SwarmLightEntry
{
    float4 color;
    float radius;
    float intensity;
    float2 _pad;
};

#ifdef SWARM_LIGHT_AREAS
StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
#else
StructuredBuffer<SwarmProjectile> projectiles : register(t0);
Buffer<uint> projStates : register(t1);
#endif
StructuredBuffer<SwarmRecipe> recipes : register(t2);
StructuredBuffer<SwarmLightEntry> lightDefs : register(t3);

RWStructuredBuffer<PointLight> outLights : register(u0);
RWStructuredBuffer<uint> outCount : register(u1);   // [0] = 有効な数

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;

    float3 srcPos;
    uint srcVfx;
    float srcScale; // サイズ倍率（拡大鏡）: 爆発が大きいほど広い円を照らす
#ifdef SWARM_LIGHT_AREAS
    if (i >= SWARM_MAX_AREAS)
        return;
    if (areaStates[i] == SWARM_DEAD)
        return;
    srcPos = areas[i].center;
    srcVfx = areas[i].vfxType;
    srcScale = SwarmAreaScale(areas[i]);
#else
    if (i >= g_MaxProjectiles)
        return;
    if (projStates[i] == SWARM_DEAD)
        return;
    srcPos = projectiles[i].position;
    srcVfx = projectiles[i].vfxType;
    srcScale = SwarmProjScale(projectiles[i]);
#endif

    SwarmRecipe r = recipes[srcVfx];
    if (r.lightCount == 0u)
        return;

    for (uint k = 0u; k < r.lightCount; ++k)
    {
        uint slot;
        InterlockedAdd(outCount[0], 1u, slot);
        if (slot >= MAX_POINT_LIGHTS)
        {
            // 上限超え: カウントが MAX 以下に収まるようスロットを返す
            InterlockedAdd(outCount[0], (uint) -1, slot);
            return;
        }
        SwarmLightEntry d = lightDefs[r.lightStart + k];
        PointLight l;
        l.position = srcPos;
        l.radius = d.radius * srcScale;
        l.color = d.color.rgb;
        l.intensity = d.intensity;
        outLights[slot] = l;
    }
}
