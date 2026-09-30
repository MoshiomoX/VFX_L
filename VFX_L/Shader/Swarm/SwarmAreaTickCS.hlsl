// ============================================================
// SwarmAreaTickCS.hlsl
// One fixed step of the area clocks. One thread per area slot.
// Runs after HitCS (so an area spawned by a hit ticks in the very
// step it was born) and before AreaDamageCS.
//
//   - an area whose time ran out dies HERE, at the start of the next
//     step, so its last tick still got applied by AreaDamageCS
//   - FOLLOW_PLAYER areas are moved onto the player
//   - CAPSULE areas (beams) take start / radius / end from SwarmBeamCB
//     (b3, written by the CPU every frame) and die when the CPU marks
//     the channel inactive. The end goes to areaEnds (u3) for AreaDamageCS
//   - tickNow = 1 marks "deals damage this step"
//
// Only writer of SwarmArea.tickNow / tickTimer / timeLeft.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

RWStructuredBuffer<SwarmArea> areas : register(u0);
RWBuffer<uint> areaStates : register(u1);
RWByteAddressBuffer counters : register(u2);
RWStructuredBuffer<float4> areaEnds : register(u3); // capsule end (xyz), only meaningful for CAPSULE areas

cbuffer SwarmBeamCB : register(b3)
{
    float4 g_BeamStart[SWARM_MAX_BEAMS]; // xyz = start, w = radius
    float4 g_BeamEnd[SWARM_MAX_BEAMS]; // xyz = end,   w = 1 while the beam is on
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= SWARM_MAX_AREAS)
        return;
    if (areaStates[i] == SWARM_DEAD)
        return;

    SwarmArea a = areas[i];

    if (a.timeLeft <= 0.0)
    {
        areaStates[i] = SWARM_DEAD;
        return;
    }

    if ((a.flags & SWARM_AREA_FOLLOW_PLAYER) != 0u)
    {
        // keep the authored height, ride on the player in XZ
        a.center.x = g_PlayerPos.x;
        a.center.z = g_PlayerPos.z;
    }
    if ((a.flags & SWARM_AREA_CAPSULE) != 0u)
    {
        uint ch = (a.flags >> SWARM_AREA_BEAM_SHIFT) & 0xFu;
        float4 s = g_BeamStart[ch];
        float4 e = g_BeamEnd[ch];
        if (ch >= SWARM_MAX_BEAMS || e.w <= 0.0)
        {
            areaStates[i] = SWARM_DEAD; // the CPU ended the beam (or never owned this channel)
            return;
        }
        a.center = s.xyz;
        a.radius = s.w;
        areaEnds[i] = float4(e.xyz, 1.0);
    }

    a.tickNow = 0u;
    a.tickTimer -= g_Step;
    if (a.tickTimer <= 0.0)
    {
        a.tickNow = 1u;
        a.tickTimer += max(a.tickInterval, g_Step);
    }

    a.timeLeft -= g_Step;

    areas[i] = a;

    uint prev;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_AREAS, 1u, prev);
    if (a.tickNow != 0u)
        counters.InterlockedAdd(SWARM_CNT_TICKING_AREAS, 1u, prev);
}
