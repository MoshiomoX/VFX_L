// ============================================================
// SwarmLiquidTrackCS.hlsl
// Once per frame, one thread per area slot (2026-10-02, Liquid entry).
// Notices areas born since last frame and keeps, per slot,
//   liquidTrack[slot] = (throw dir x, throw dir z,
//                        timeLeft when first seen, timeLeft last frame)
// so SwarmLiquidVS knows a puddle's age (= first seen - now) and which way it
// was thrown. No clock is needed: a slot holds a new area when it was empty
// last frame (w <= 0) or its timeLeft went UP.
//
// The direction comes from whoever spawned the area: SwarmSpawnAreaFromDef
// with SWARM_AREA_DIR_U (ProjMoveCS, so a LOB flask landing) writes
// areaDirs[slot] = (dir, 0, 1). It is taken only while w = 1 ("fresh") and
// w is cleared here, so a later area in the same slot spawned without a
// direction (HitCS has no UAV left) cannot inherit a stale one: it gets
// (0, 0) and the puddle picks an angle from its seed. Every area is
// tracked, liquid or not, so no fresh flag is ever left behind.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
RWStructuredBuffer<float4> areaDirs : register(u0);
RWStructuredBuffer<float4> liquidTrack : register(u1);
// Areas ever born (2026-10-03), grows for good, the CPU reads the difference:
//   [0]            = SWARM_AREA_SHAKE areas (SwarmSystem::ConsumeShakeAreas -> BattleCamera)
//   [1 + vfxType]  = areas per GPU VFX recipe (hit sparks, explosions, death puffs, pools)
//                    -> SwarmSystem::ConsumeAreaBirths -> sounds (the CPU never sees GPU hits)
// Counted here because this pass already spots every newly born area once
static const uint SWARM_AREA_BIRTH_KINDS = 127u;   // = Swarm::kAreaBirthKinds
RWByteAddressBuffer areaBirths : register(u2);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= SWARM_MAX_AREAS)
        return;

    float4 t = liquidTrack[i];
    if (areaStates[i] == SWARM_DEAD)
    {
        t.w = 0.0;   // empty
        liquidTrack[i] = t;
        return;
    }

    float left = areas[i].timeLeft;
    if (t.w <= 0.0 || left > t.w + 1e-3)
    {
        float4 ad = areaDirs[i];
        float2 dir = (ad.w > 0.5) ? ad.xy : float2(0.0, 0.0);
        areaDirs[i] = float4(ad.xyz, 0.0);   // consumed
        t = float4(dir, left, left);
        uint prev;
        if ((areas[i].flags & SWARM_AREA_SHAKE) != 0u)
            areaBirths.InterlockedAdd(0, 1u, prev);
        uint kind = min(areas[i].vfxType, SWARM_AREA_BIRTH_KINDS - 1u);
        areaBirths.InterlockedAdd((1u + kind) * 4u, 1u, prev);
    }
    else
    {
        t.w = left;
    }
    liquidTrack[i] = t;
}
