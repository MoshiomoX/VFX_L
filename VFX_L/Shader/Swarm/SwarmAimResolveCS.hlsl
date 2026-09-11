// ============================================================
// SwarmAimResolveCS.hlsl
// Runs right after EnemyMoveCS, before ProjMove / Hit. The key was
// written by an enemy that was alive at Move time; if HitCS kills it
// later in the same step the CPU fires one shot at a corpse, which
// is harmless. Not checking state here avoids reporting "no target"
// every time the closest enemy dies.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
RWByteAddressBuffer counters : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    uint key = counters.Load(SWARM_CNT_NEAREST_KEY);
    uint slot = key & SWARM_SLOT_MASK;

    if (key == SWARM_NO_TARGET_KEY )
    {
        counters.Store(SWARM_CNT_NEAREST_DIST, asuint(1e30));
        return;
    }

    SwarmEnemy e = enemies[slot];
    float3 dp = e.position - g_PlayerPos;
    float dist = sqrt(dp.x * dp.x + dp.z * dp.z);

    counters.Store3(SWARM_CNT_NEAREST_POS, asuint(e.position));
    counters.Store3(SWARM_CNT_NEAREST_VEL, asuint(e.velocity));
    counters.Store(SWARM_CNT_NEAREST_DIST, asuint(dist));
}