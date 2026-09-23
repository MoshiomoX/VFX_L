// ============================================================
// SwarmAreaTickCS.hlsl
// One fixed step of the area clocks. One thread per area slot.
// Runs after HitCS (so an area spawned by a hit ticks in the very
// step it was born) and before AreaDamageCS.
//
//   - an area whose time ran out dies HERE, at the start of the next
//     step, so its last tick still got applied by AreaDamageCS
//   - FOLLOW_PLAYER areas are moved onto the player
//   - tickNow = 1 marks "deals damage this step"
//
// Only writer of SwarmArea.tickNow / tickTimer / timeLeft.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

RWStructuredBuffer<SwarmArea> areas : register(u0);
RWBuffer<uint> areaStates : register(u1);
RWByteAddressBuffer counters : register(u2);

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
