// ============================================================
// SwarmProjTrailCS.hlsl
// Ribbon trail of GPU projectiles (2026-10-15). One thread per
// projectile slot, run every fixed step right after ProjMoveCS.
//
// Every slot owns a ring of TRAIL_POINTS positions (same layout and
// style table as the particle trails, ParticleTrail.hlsli). A slot
// whose recipe has a Trail entry (recipeTrail[vfxType] != 0) pushes
// its position into the ring whenever its age crosses the sampling
// grid of lifetime / (TRAIL_POINTS - 1). A dead slot is reset, so the
// next projectile that lands in it starts a fresh ribbon. SwarmSpawnProjCS
// also clears the state when it reuses a slot (2026-10-25): a shot killed
// by HitCS in the last step of a frame is reused before this pass has seen
// it die, and the new ribbon used to start from the old shot's points.
//
// trailState : bits 0-7 ring write position, bits 8-15 valid samples,
//              bit 16 = draining (the projectile died; the ribbon keeps
//              being fed with the impact point so the tail catches up =
//              "sinks into the enemy"), bits 17-24 = samples pushed
//              since the death (TRAIL_POINTS of them = fully drained)
// trailAge   : seconds since the slot became alive (the grid clock)
// ============================================================
#include "../Common/SwarmCommon.hlsli"
#include "../Particle/Common/ParticleTrail.hlsli"
#include "../Common/SwarmProjTrail.hlsli"

StructuredBuffer<uint> recipeTrail : register(t0);
StructuredBuffer<TrailStyle> trailStyles : register(t1);
StructuredBuffer<SwarmProjectile> projectiles : register(t2);
Buffer<uint> projStates : register(t3);

RWStructuredBuffer<float3> trailPoints : register(u0);
RWStructuredBuffer<uint> trailState : register(u1);
RWStructuredBuffer<float> trailAge : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxProjectiles)
        return;

    uint state = trailState[i];
    bool alive = projStates[i] != SWARM_DEAD;
    if (!alive && state == 0u)
        return;

    // the slot keeps the projectile's last data after it dies (until it is reused)
    SwarmProjectile p = projectiles[i];
    uint style = recipeTrail[p.vfxType];
    if (style == 0u)
    {
        if (state != 0u)
        {
            trailState[i] = 0u;
            trailAge[i] = 0.0;
        }
        return;
    }

    bool draining = (state & PROJ_TRAIL_DRAINING) != 0u;
    if (alive && draining)
    {
        // the slot was reused before the old ribbon drained away: start fresh
        state = 0u;
        draining = false;
        trailAge[i] = 0.0;
    }
    if (!alive)
        draining = true; // just died, or still draining

    TrailStyle s = trailStyles[style - 1u];
    float interval = TrailInterval(s.lifetime);

    float agePrev = trailAge[i];
    float age = agePrev + g_Step;
    trailAge[i] = age;

    uint head = TrailHead(state);
    uint count = TrailCount(state);
    uint drained = state >> PROJ_TRAIL_DRAIN_SHIFT;

    uint cellNow = (uint) (age / interval);
    uint cellPrev = (uint) (agePrev / interval);

    // count == 0: first step alive -> record where it started.
    // draining: the head stays at the impact point; each push replaces the
    // oldest sample with it, so the ribbon shortens from the tail
    if (count == 0u || cellNow != cellPrev)
    {
        head = (count == 0u) ? 0u : (head + 1u) % TRAIL_POINTS;
        trailPoints[i * TRAIL_POINTS + head] = p.position;
        count = min(count + 1u, TRAIL_POINTS);
        if (draining)
            drained += 1u;
    }

    if (draining && drained >= TRAIL_POINTS)
    {
        trailState[i] = 0u; // every sample is the impact point now: gone
        trailAge[i] = 0.0;
        return;
    }
    trailState[i] = TrailPack(head, count)
        | (draining ? PROJ_TRAIL_DRAINING : 0u)
        | (drained << PROJ_TRAIL_DRAIN_SHIFT);
}
