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
// The append counters are reset by binding the UAVs with initial count 0.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t1);
AppendStructuredBuffer<uint> aliveList : register(u0);
AppendStructuredBuffer<uint> mobList : register(u1);
AppendStructuredBuffer<uint> bomberList : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    aliveList.Append(i);
    if (enemyExtra[i].kind == SWARM_KIND_BOMBER)
        bomberList.Append(i);
    else
        mobList.Append(i);
}
