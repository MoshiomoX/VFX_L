// ============================================================
// SwarmSpawnCS.hlsl
// Push pending spawn requests into free slots of the pool.
//
// One thread per request. Each thread scans for a dead slot and
// claims it with InterlockedCompareExchange on the state buffer.
//
// Why a linear scan instead of a free list:
//   a consume buffer needs CopyStructureCount every frame, which
//   flushes the command queue (~0.076ms measured in the particle
//   system). Requests per frame are few, so scanning is cheaper.
//
// The scan starts at a per-thread offset so threads do not all
// fight over slot 0.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmProjectile> spawnRequests : register(t0);
RWStructuredBuffer<SwarmProjectile> projectiles : register(u0);
RWBuffer<uint> projStates : register(u1);

cbuffer SwarmSpawnCB : register(b1)
{
    uint g_RequestCount;
    uint g_ScanStart;
    uint2 _spawnPad;
};
    
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_RequestCount)
        return;

    SwarmProjectile req = spawnRequests[id.x];

    // spread starting points so threads claim different regions
    uint start = (g_ScanStart + id.x * 97u) % g_MaxProjectiles;

    for (uint i = 0; i < g_MaxProjectiles; ++i)
    {
        uint slot = (start + i) % g_MaxProjectiles;

        uint prev;
        InterlockedCompareExchange(projStates[slot],
                                   SWARM_DEAD, SWARM_ALIVE, prev);

        if (prev == SWARM_DEAD)
        {
            // won the slot. state is already ALIVE from the exchange
            projectiles[slot] = req;
            return;
        }
    }

    // pool full: drop it. same policy as the particle system's
    // deadCount guard -- degrade, never corrupt
}