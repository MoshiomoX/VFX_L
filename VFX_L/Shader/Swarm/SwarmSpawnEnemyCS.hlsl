// ============================================================
// SwarmSpawnEnemyCS.hlsl
// Push pending enemy spawn requests into free slots of the pool.
//
// Same skeleton as SwarmSpawnProjCS: one thread per request,
// linear scan from a per-thread offset, claim with
// InterlockedCompareExchange on the state buffer.
//
// The request already carries position / hp / moveSpeed from the
// CPU. velocity and yaw start at zero; the AI pass fills them in
// on the next fixed step.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> spawnRequests : register(t0);
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWBuffer<uint> enemyStates : register(u1);

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

    SwarmEnemy req = spawnRequests[id.x];

    // spread starting points so threads claim different regions
    uint start = (g_ScanStart + id.x * 97u) % g_MaxEnemies;

    for (uint i = 0; i < g_MaxEnemies; ++i)
    {
        uint slot = (start + i) % g_MaxEnemies;

        uint prev;
        InterlockedCompareExchange(enemyStates[slot],
                                   SWARM_DEAD, SWARM_ALIVE, prev);

        if (prev == SWARM_DEAD)
        {
            // won the slot. state is already ALIVE from the exchange
            enemies[slot] = req;
            return;
        }
    }

    // pool full: drop it. same policy as the projectile pool --
    // degrade, never corrupt
}