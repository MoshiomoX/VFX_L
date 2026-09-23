// ============================================================
// SwarmSpawnAreaCS.hlsl
// Push the CPU's pending area requests into free slots of the
// area pool. One thread per request, same CAS scan as the other
// spawn shaders. Pool full -> the request is dropped.
//
// Areas born on the GPU (projectile hit) do not come through here;
// see SwarmSpawnAreaFromDef in SwarmCommon.hlsli.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmArea> spawnRequests : register(t0);
RWStructuredBuffer<SwarmArea> areas : register(u0);
RWBuffer<uint> areaStates : register(u1);

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

    SwarmArea req = spawnRequests[id.x];
    req.tickNow = 0u;

    uint start = (g_ScanStart + id.x * 97u) % SWARM_MAX_AREAS;
    for (uint k = 0; k < SWARM_MAX_AREAS; ++k)
    {
        uint slot = (start + k) % SWARM_MAX_AREAS;
        uint was;
        InterlockedCompareExchange(areaStates[slot], SWARM_DEAD, SWARM_ALIVE, was);
        if (was == SWARM_DEAD)
        {
            areas[slot] = req;
            return;
        }
    }
}
