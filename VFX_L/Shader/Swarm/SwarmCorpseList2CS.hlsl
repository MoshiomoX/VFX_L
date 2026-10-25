// ============================================================
// SwarmCorpseList2CS.hlsl
// The second half of SwarmCorpseListCS (2026-10-25): that pass already
// binds 8 UAVs, and the boss, the sand mage and the dash bomber used to
// shatter in a borrowed skin (the zombie's / the red robot's) because of
// it. Here they and the ruins enemies get their own lists, so every kind
// shatters in its own skin. Same rule: entries still shattering
// (0 <= age < g_CorpseLife) go into the list of their look.
// (The cursed armor's first death never writes a corpse: it collapses.)
// ============================================================
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmCorpseListCB : register(b4)
{
    float g_CorpseTime;
    float g_CorpseLife;
    float2 _corpseListPad;
};

StructuredBuffer<SwarmCorpse> corpses : register(t0);
AppendStructuredBuffer<uint> bossCorpses : register(u0);
AppendStructuredBuffer<uint> sandMageCorpses : register(u1);
AppendStructuredBuffer<uint> dashBomberCorpses : register(u2);
AppendStructuredBuffer<uint> armorCorpses : register(u3);
AppendStructuredBuffer<uint> gargoyleCorpses : register(u4);
AppendStructuredBuffer<uint> necroCorpses : register(u5);
AppendStructuredBuffer<uint> skeletonCorpses : register(u6);
AppendStructuredBuffer<uint> wardenCorpses : register(u7);

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

    if (c.kind == SWARM_KIND_BOSS)
        bossCorpses.Append(i);
    else if (c.kind == SWARM_KIND_SANDMAGE)
        sandMageCorpses.Append(i);
    else if (c.kind == SWARM_KIND_DASHBOMBER)
        dashBomberCorpses.Append(i);
    else if (c.kind == SWARM_KIND_ARMOR || c.kind == SWARM_KIND_ARMOR2)
        armorCorpses.Append(i);
    else if (c.kind == SWARM_KIND_GARGOYLE)
        gargoyleCorpses.Append(i);
    else if (c.kind == SWARM_KIND_NECRO)
        necroCorpses.Append(i);
    else if (c.kind == SWARM_KIND_SKELETON)
        skeletonCorpses.Append(i);
    else if (c.kind == SWARM_KIND_WARDEN)
        wardenCorpses.Append(i);
}
