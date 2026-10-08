// ============================================================
// SwarmCorpseListCS.hlsl
// Once per frame, one thread per corpse ring entry (2026-10-02, death
// shatter): entries still shattering (0 <= age < g_CorpseLife) go into the
// draw list of their look, exactly like SwarmEnemyCompactCS sorts live
// enemies: mobs / elites / the boss share the mob texture, bombers have
// their own, ghosts are drawn translucent. The list lengths become the
// InstanceCount of each submesh's DrawIndexedInstancedIndirect.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmCorpseListCB : register(b4)
{
    float g_CorpseTime;
    float g_CorpseLife;
    float2 _corpseListPad;
};

StructuredBuffer<SwarmCorpse> corpses : register(t0);
AppendStructuredBuffer<uint> mobCorpses : register(u0);
AppendStructuredBuffer<uint> bomberCorpses : register(u1);
AppendStructuredBuffer<uint> ghostCorpses : register(u2);
AppendStructuredBuffer<uint> splitterCorpses : register(u3);
AppendStructuredBuffer<uint> bruteCorpses : register(u4);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= SWARM_MAX_CORPSES)
        return;
    SwarmCorpse c = corpses[i];
    float age = g_CorpseTime - c.birth;
    if (c.birth <= 0.0 || age < 0.0 || age >= g_CorpseLife)
        return;

    if (c.kind == SWARM_KIND_BOMBER)
        bomberCorpses.Append(i);
    else if (c.kind == SWARM_KIND_GHOST)
        ghostCorpses.Append(i);
    else if (c.kind == SWARM_KIND_SPLITTER || c.kind == SWARM_KIND_SPLITLING)
        splitterCorpses.Append(i);
    else if (c.kind == SWARM_KIND_BRUTE)
        bruteCorpses.Append(i);
    else
        mobCorpses.Append(i);
}
