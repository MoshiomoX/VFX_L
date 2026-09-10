// ============================================================
// SwarmProjMoveCS.hlsl
// One fixed step of projectile integration.
//
// g_Step is always the same value here -- that is the point of the
// fixed step. The result cannot depend on frame rate.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<uint> terrain : register(t0);
RWStructuredBuffer<SwarmProjectile> projectiles : register(u0);
RWBuffer<uint> projStates : register(u1);
RWByteAddressBuffer counters : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxProjectiles)
        return;

    if (projStates[i] == SWARM_DEAD)
        return;

    SwarmProjectile p = projectiles[i];

    // ---- lifetime ----
    p.lifetime -= g_Step;
    if (p.lifetime <= 0.0)
    {
        projStates[i] = SWARM_DEAD;
        return;
    }

    // ---- integrate ----
    // no gravity: projectiles fly straight (same as the CPU version)
    p.position += p.velocity * g_Step;

    // ---- terrain ----
    // entering a blocked cell kills it. grid precision is 2m but a
    // wall is 2m thick anyway, so nothing slips through
    if (!SwarmIsWalkable(terrain, p.position))
    {
        projStates[i] = SWARM_DEAD;
        return;
    }

    projectiles[i].position = p.position;
    projectiles[i].lifetime = p.lifetime;
    uint prevCount;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_PROJ, 1u, prevCount);
}