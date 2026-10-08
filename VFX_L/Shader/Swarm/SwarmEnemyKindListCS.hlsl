// ============================================================
// SwarmEnemyKindListCS.hlsl
// The draw lists of the kinds that did not fit SwarmEnemyCompactCS
// (2026-10-08): that pass already binds 8 UAVs, the most a compute
// shader gets on feature level 11.0. Same rule as there: one thread per
// enemy slot, live slots of a kind are appended to its list, the list
// length becomes the InstanceCount of each submesh's indirect draw.
// CompactCS leaves these kinds out of mobList. Runs right after it.
//   chargerList : SWARM_KIND_CHARGER (own texture, the dash band)
//   shieldList  : SWARM_KIND_SHIELD  (own texture, the tower shield)
// The append counters are reset by binding the UAVs with initial count 0.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t1);
AppendStructuredBuffer<uint> chargerList : register(u0);
AppendStructuredBuffer<uint> shieldList : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    uint kind = enemyExtra[i].kind;
    if (kind == SWARM_KIND_CHARGER)
        chargerList.Append(i);
    else if (kind == SWARM_KIND_SHIELD)
        shieldList.Append(i);
}
