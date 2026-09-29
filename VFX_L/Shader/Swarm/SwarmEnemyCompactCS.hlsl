// ============================================================
// SwarmEnemyCompactCS.hlsl
// One thread per enemy slot: append the index of every live slot to
// aliveList. The mesh draw then goes through DrawIndexedInstancedIndirect
// with instanceCount = list size (CopyStructureCount), so the vertex
// shader only runs for enemies that exist instead of the whole 4096
// slot pool. With a ~9k vertex mesh that is the difference between
// 35M and ~1M vertices a frame.
//
// Each slot also goes to the list of its kind (mobList / bomberList):
// the kinds share the mesh but not the texture, so the mesh is drawn
// once per kind list. aliveList (all kinds) feeds the HP bars.
//
// Elites and the boss share the mob mesh + texture, so they go to mobList
// (the VS scales and tints them by kind).
//
// The boss also reports itself to bossInfo (Swarm::BossInfo, 32 bytes,
// cleared by the CPU before this dispatch, read back through staging):
//   +0 alive count, +4 hp (fixed), +8 max hp (fixed), +16 position
//
// The append counters are reset by binding the UAVs with initial count 0.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t1);
StructuredBuffer<SwarmEnemy> enemies : register(t2);
StructuredBuffer<uint> enemyMaxHp : register(t3);
AppendStructuredBuffer<uint> aliveList : register(u0);
AppendStructuredBuffer<uint> mobList : register(u1);
AppendStructuredBuffer<uint> bomberList : register(u2);
RWByteAddressBuffer bossInfo : register(u3);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    aliveList.Append(i);
    uint kind = enemyExtra[i].kind;
    if (kind == SWARM_KIND_BOMBER)
        bomberList.Append(i);
    else
        mobList.Append(i);

    if (kind == SWARM_KIND_BOSS)
    {
        uint prev;
        bossInfo.InterlockedAdd(0, 1u, prev);
        SwarmEnemy e = enemies[i];
        bossInfo.Store(4, e.hp);
        bossInfo.Store(8, enemyMaxHp[i]);
        bossInfo.Store3(16, asuint(e.position));
    }
}
