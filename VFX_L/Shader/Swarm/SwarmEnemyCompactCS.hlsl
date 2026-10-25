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
// Elites share the mob mesh + texture, so they go to mobList (the VS
// scales and tints them by kind). The boss has its own list since
// 2026-10-24 (SwarmEnemyKindListCS: the stage's skin + decorations).
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
AppendStructuredBuffer<uint> ghostList : register(u4); // drawn last with alpha blend
AppendStructuredBuffer<uint> splitterList : register(u5); // splitters and splitlings (own texture, 2026-10-03)
AppendStructuredBuffer<uint> bruteList : register(u6); // brutes (own texture, 2026-10-07)
// chargers and shield bearers (2026-10-08) go to SwarmEnemyKindListCS: this pass has no UAV left (8 on FL 11.0)

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
    else if (SwarmIsGhost(kind))
        ghostList.Append(i);   // the dark ghost shares the list (same mesh, drawn translucent; the VS tints it by kind)
    else if (kind == SWARM_KIND_SPLITTER || kind == SWARM_KIND_SPLITLING)
        splitterList.Append(i);
    else if (kind == SWARM_KIND_BRUTE)
        bruteList.Append(i);
    else if (kind != SWARM_KIND_CHARGER && kind != SWARM_KIND_SHIELD && kind != SWARM_KIND_SPIKEBALL && kind != SWARM_KIND_BURROWER
          && kind != SWARM_KIND_SANDMAGE && kind != SWARM_KIND_TORNADO && kind != SWARM_KIND_DASHBOMBER
          && kind != SWARM_KIND_BOSS && !SwarmInKindList2(kind)) // spike balls have no blocky body (SwarmSpikeBallVS), tornadoes draw nothing (2026-10-20); the rest have their own list in SwarmEnemyKindListCS (the boss too, 2026-10-24: its own skin + decorations per stage)
        mobList.Append(i);

    // the final-swarm ghosts are counted per kind in the two spare words of bossInfo (offsets 12 / 28):
    // the CPU gives each its own cap (2026-10-18; before, ghosts shared the mob cap and barely spawned)
    if (kind == SWARM_KIND_GHOST)
    {
        uint prev;
        bossInfo.InterlockedAdd(12, 1u, prev);
    }
    else if (kind == SWARM_KIND_DARKGHOST)
    {
        uint prev;
        bossInfo.InterlockedAdd(28, 1u, prev);
    }
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
