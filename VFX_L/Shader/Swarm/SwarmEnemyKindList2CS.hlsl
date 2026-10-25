// ============================================================
// SwarmEnemyKindList2CS.hlsl
// The draw lists of the ruins enemies (2026-10-25): SwarmEnemyCompactCS
// and SwarmEnemyKindListCS already bind 8 UAVs each. Same rule: one
// thread per enemy slot, live slots of a kind are appended to its list,
// the list length becomes the InstanceCount of each submesh's indirect
// draw. CompactCS leaves these kinds out of mobList. Runs right after
// SwarmEnemyKindListCS.
//   armorList    : SWARM_KIND_ARMOR and ARMOR2 (the revived one glows)
//   gargoyleList : SWARM_KIND_GARGOYLE (wings)
//   necroList    : SWARM_KIND_NECRO (hood, staff)
//   skeletonList : SWARM_KIND_SKELETON
//   wardenList   : SWARM_KIND_WARDEN (chains, hook)
//   eliteAList   : SWARM_KIND_ELITE_A, the stage's melee fixed elite (2026-10-25)
//   eliteBList   : SWARM_KIND_ELITE_B, the ranged one
// The fixed elites also report themselves to eliteInfo (Swarm::EliteInfoBlock: +0 count, then
// 32 bytes each = slot, kind, hp (fixed), max hp (fixed), position, fuse; cleared by the CPU
// before this dispatch, read back through staging by EliteAttacks)
// The append counters are reset by binding the UAVs with initial count 0.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t1);
StructuredBuffer<SwarmEnemy> enemies : register(t2);
StructuredBuffer<uint> enemyMaxHp : register(t3);
AppendStructuredBuffer<uint> armorList : register(u0);
AppendStructuredBuffer<uint> gargoyleList : register(u1);
AppendStructuredBuffer<uint> necroList : register(u2);
AppendStructuredBuffer<uint> skeletonList : register(u3);
AppendStructuredBuffer<uint> wardenList : register(u4);
AppendStructuredBuffer<uint> eliteAList : register(u5);
AppendStructuredBuffer<uint> eliteBList : register(u6);
RWByteAddressBuffer eliteInfo : register(u7);

static const uint ELITE_INFO_MAX = 4u;   // = Swarm::kMaxEliteInfo

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    uint kind = enemyExtra[i].kind;
    if (kind == SWARM_KIND_ARMOR || kind == SWARM_KIND_ARMOR2)
        armorList.Append(i);
    else if (kind == SWARM_KIND_GARGOYLE)
        gargoyleList.Append(i);
    else if (kind == SWARM_KIND_NECRO)
        necroList.Append(i);
    else if (kind == SWARM_KIND_SKELETON)
        skeletonList.Append(i);
    else if (kind == SWARM_KIND_WARDEN)
        wardenList.Append(i);
    else if (SwarmIsFixedElite(kind))
    {
        if (kind == SWARM_KIND_ELITE_A)
            eliteAList.Append(i);
        else
            eliteBList.Append(i);
        uint n;
        eliteInfo.InterlockedAdd(0, 1u, n);
        if (n < ELITE_INFO_MAX)
        {
            uint at = 16u + n * 32u;
            SwarmEnemy e = enemies[i];
            eliteInfo.Store4(at, uint4(i, kind, e.hp, enemyMaxHp[i]));
            eliteInfo.Store4(at + 16u, uint4(asuint(e.position), asuint(enemyExtra[i].fuse)));
        }
    }
}
