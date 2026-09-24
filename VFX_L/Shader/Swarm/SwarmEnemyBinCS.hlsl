// ============================================================
// SwarmEnemyBinCS.hlsl
// One thread per enemy slot: drop every live enemy into the spatial
// hash bucket of its cell (see SWARM_BUCKET_CAP in SwarmCommon).
// cellCount is cleared to 0 by the CPU (ClearUnorderedAccessViewUint)
// right before this dispatch. Runs first in every fixed step so the
// AI and the push pass read one consistent snapshot.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<SwarmEnemy> enemies : register(t1);

RWStructuredBuffer<uint> cellCount : register(u0);
RWStructuredBuffer<uint> cellItems : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    int cell = SwarmCellOf(enemies[i].position);
    if (cell < 0)
        return;

    uint slot;
    InterlockedAdd(cellCount[cell], 1u, slot);
    if (slot < SWARM_BUCKET_CAP)
        cellItems[(uint) cell * SWARM_BUCKET_CAP + slot] = i;
}
