// ============================================================
// SwarmEmitCS.hlsl
// One thread per GPU projectile. If alive, walk its recipe and emit
// from every emitter in it at the projectile's position.
//
// The projectile is only a core (position + judgement). Everything
// visible is particles, same as the CPU path.
//
// Dead-list guard: one thread emits k particles, so reserve k from a
// per-frame budget atomically and bail when the reservation exceeds
// the free count. The reservation keeps the consume counter from
// underflowing when many threads race.
// ============================================================
// particle init / shapes / StochasticCount are shared with GrassFireEmitCS (EmitInit.hlsli)
#include "../Particle/Common/EmitInit.hlsli"

// ParticleCommon owns b0 (GlobalCB) and b1 (DeadListCB); ours goes to b2
#define SWARM_FRAME_CB_REG b2
#include "../Common/SwarmCommon.hlsli"

// SwarmAreaEmitCS.hlsl #defines SWARM_EMIT_AREAS and includes this file:
// same emit code, but the sources are the live areas instead of projectiles.
// SwarmOrbEmitCS.hlsl does the same with SWARM_EMIT_ORBS: the sources are
// the orbs being pulled toward the player, all with one recipe (b3)
#if defined(SWARM_EMIT_AREAS)
StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
#elif defined(SWARM_EMIT_ORBS)
StructuredBuffer<SwarmOrb> orbs : register(t0);
Buffer<uint> orbStates : register(t1);

// Must match SwarmSystem::OrbEmitCB
cbuffer SwarmOrbEmitCB : register(b3)
{
    uint g_OrbTrailVfx;        // recipe index (SwarmVFXTable::IndexOf). 0 = none
    float g_OrbTrailMinSpeed;  // pulled faster than this (m/s) -> emits
    uint2 _orbEmitPad;
};
#else
StructuredBuffer<SwarmProjectile> projectiles : register(t0);
Buffer<uint> projStates : register(t1);
#endif
StructuredBuffer<SwarmRecipe> recipes : register(t2);
StructuredBuffer<GPUEmitter> emitters : register(t3);
Buffer<uint> deadCount : register(t4);

RWStructuredBuffer<GPUParticle> particles : register(u0);
ConsumeStructuredBuffer<uint> deadList : register(u1);
RWByteAddressBuffer emitBudget : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;

    // ---- the source: where, how fast, which recipe ----
    float3 srcPos;
    float3 srcVel;
    uint srcVfx;
    float srcScale = 1.0; // size scale (Magnifier). 1 = as authored
#if defined(SWARM_EMIT_AREAS)
    if (i >= SWARM_MAX_AREAS)
        return;
    if (areaStates[i] == SWARM_DEAD)
        return;
    srcPos = areas[i].center;
    srcVel = float3(0, 0, 0);
    srcVfx = areas[i].vfxType;
    srcScale = SwarmAreaScale(areas[i]);
#elif defined(SWARM_EMIT_ORBS)
    if (i >= g_MaxOrbs || g_OrbTrailVfx == 0u)
        return;
    if (orbStates[i] == SWARM_DEAD)
        return;
    if (orbs[i]._pad <= g_OrbTrailMinSpeed) // _pad = pull speed (SwarmOrbMoveCS)
        return;
    srcPos = orbs[i].position;
    srcVel = orbs[i].velocity;
    srcVfx = g_OrbTrailVfx;
#else
    if (i >= g_MaxProjectiles)
        return;
    if (projStates[i] == SWARM_DEAD)
        return;
    srcPos = projectiles[i].position;
    srcVel = projectiles[i].velocity;
    srcVfx = projectiles[i].vfxType;
    srcScale = SwarmProjScale(projectiles[i]);
#endif

    SwarmRecipe r = recipes[srcVfx];
    if (r.particleCount == 0u)
        return;

    uint seed = g_BaseSeed + i * 7919u;

    for (uint ei = 0u; ei < r.particleCount; ++ei)
    {
        GPUEmitter e = emitters[r.particleStart + ei];
        // bigger source = the whole effect scaled: offset, emit shape, speed (how far it
        // spreads in its lifetime), gravity, particle size. A little denser too (x scale;
        // the area grows with scale^2 but the particle pool is shared)
        e.position *= srcScale;
        e.shapeSize *= srcScale;
        e.speedRange *= srcScale;
        e.gravity *= srcScale;
        e.sizeRange *= srcScale;
        e.emitRate *= srcScale;
        e.position = srcPos + e.position; // follows the source; the entry position is an offset (layers)

        uint k = StochasticCount(e.emitRate, g_DeltaTime, seed);
        if (k == 0u)
            continue;

        // ---- reserve from the free pool ----
        uint mine;
        emitBudget.InterlockedAdd(0, k, mine);
        uint freeNow = deadCount[0];
        if (mine >= freeNow)
            return;
        if (mine + k > freeNow)
            k = freeNow - mine;

        for (uint n = 0u; n < k; ++n)
        {
            uint particleIndex = deadList.Consume();
            uint s = seed + (ei * 131u + n) * 1099u;

            float3 pos = e.position;
            float3 vel = float3(0, 0, 0);
            EmitShape(e, s, pos, vel);

            // sweep emit: the projectile moved about velocity * dt since the
            // last emit, so scatter back along that segment. Without this a
            // fast projectile leaves a dotted trail. Own seed so the
            // attribute stream below stays unchanged
            uint sweepSeed = s ^ 0x9E3779B9u;
            pos -= srcVel * (g_DeltaTime * Random(sweepSeed));

            // flies along with the source (arrow body etc.)
            if ((((uint) e.renderMode) & PARTICLE_INHERIT_SOURCE_VELOCITY) != 0u)
                vel += srcVel;

            // ---- particle init: identical to ParticleEmitCS (EmitInit.hlsli) ----
            particles[particleIndex] = InitParticleFromEmitter(e, s, pos, vel);
        }
    }
}