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
AppendStructuredBuffer<uint> spikeBallList : register(u2); // SWARM_KIND_SPIKEBALL: own mesh, no blocky body (2026-10-17)
AppendStructuredBuffer<uint> burrowerList : register(u3); // SWARM_KIND_BURROWER (own texture + the mound) (2026-10-17)
AppendStructuredBuffer<uint> sandMageList : register(u4); // SWARM_KIND_SANDMAGE (own texture) (2026-10-17)
AppendStructuredBuffer<uint> tornadoList : register(u5);  // SWARM_KIND_TORNADO: own mesh (SwarmTornadoVS), no blocky body (2026-10-17)
AppendStructuredBuffer<uint> dashBomberList : register(u6); // SWARM_KIND_DASHBOMBER (own texture) (2026-10-18)

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
    else if (kind == SWARM_KIND_SPIKEBALL)
        spikeBallList.Append(i);
    else if (kind == SWARM_KIND_BURROWER)
        burrowerList.Append(i);
    else if (kind == SWARM_KIND_SANDMAGE)
        sandMageList.Append(i);
    else if (kind == SWARM_KIND_TORNADO)
        tornadoList.Append(i);
    else if (kind == SWARM_KIND_DASHBOMBER)
        dashBomberList.Append(i);
}
