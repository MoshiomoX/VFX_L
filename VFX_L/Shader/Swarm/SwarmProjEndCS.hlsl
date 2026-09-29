// ============================================================
// SwarmProjEndCS.hlsl
// Step 4b of the fixed step (right after the hit test): report the
// tagged projectiles that ended this step.
//
// A basic spell (fireball / stone shot) that drives an advanced spell
// (meteor) is spawned with a trigger tag (projTags, written by
// SwarmSpawnProjCS). When such a projectile dies -- hit an enemy
// (HitCS), ran out of lifetime or hit a wall (ProjMoveCS) -- this pass
// writes where it ended into the trigger ring and clears the tag. The
// CPU reads the ring back and drops the meteor there (WeaponSystem).
//
// Why a separate pass instead of writing in HitCS / ProjMoveCS:
//   HitCS already binds 8 UAVs (the D3D11.0 limit), and the three
//   places a projectile can die would all need the same code.
//   Neither shader moves the position of a projectile that dies, so
//   projectiles[i].position is where it ended.
//
// The position is put on the ground (terrain height); the CPU lifts it
// to enemy-center height like the meteor's normal target.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmProjectile> projectiles : register(t0);
Buffer<uint> projStates : register(t1);
StructuredBuffer<float> terrainHeight : register(t2);

RWBuffer<uint> projTags : register(u0);
RWByteAddressBuffer triggerEvents : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxProjectiles)
        return;

    uint tag = projTags[i];
    if (tag == 0u)
        return;
    if (projStates[i] != SWARM_DEAD)
        return;

    projTags[i] = 0u;

    float3 pos = projectiles[i].position;
    pos.y = SwarmTerrainHeight(terrainHeight, pos.xz);

    // [0] counts every event ever written (never cleared); the CPU keeps
    // how far it has read, so a missed readback frame loses nothing
    uint n;
    triggerEvents.InterlockedAdd(0, 1u, n);
    uint offset = 16u + (n % SWARM_MAX_TRIGGER_EVENTS) * 16u;
    triggerEvents.Store4(offset, uint4(asuint(pos), tag));
}
