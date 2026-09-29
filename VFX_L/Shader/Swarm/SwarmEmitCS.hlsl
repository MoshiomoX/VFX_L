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
#include "../Particle/Common/ParticleCommon.hlsli"
#include "../Particle/Common/EmitPoint.hlsli"
#include "../Particle/Common/EmitSphere.hlsli"
#include "../Particle/Common/EmitCone.hlsli"
#include "../Particle/Common/EmitBox.hlsli"
#include "../Particle/Common/EmitRing.hlsli"
#include "../Particle/Common/EmitDisc.hlsli"

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

// emit count from a continuous rate without drift
uint StochasticCount(float rate, float dt, inout uint seed)
{
    float f = rate * dt;
    uint k = (uint) floor(f);
    if (Random(seed) < frac(f))
        k += 1u; // ← RandomFloat(seed) から修正
    return k;
}
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

            switch (e.emitType)
            {
                case 0:
                    EmitPoint(e, s, pos, vel);
                    break;
                case 1:
                    EmitSphere(e, s, pos, vel);
                    break;
                case 2:
                    EmitCone(e, s, pos, vel);
                    break;
                case 3:
                    EmitBox(e, s, pos, vel);
                    break;
                case 4:
                    EmitRing(e, s, pos, vel);
                    break;
                case 5:
                    EmitDisc(e, s, pos, vel);
                    break;
                default:
                    break; // mesh emit not supported on this path
            }

            // sweep emit: the projectile moved about velocity * dt since the
            // last emit, so scatter back along that segment. Without this a
            // fast projectile leaves a dotted trail. Own seed so the
            // attribute stream below stays unchanged
            uint sweepSeed = s ^ 0x9E3779B9u;
            pos -= srcVel * (g_DeltaTime * Random(sweepSeed));

            // flies along with the source (arrow body etc.)
            if ((((uint) e.renderMode) & PARTICLE_INHERIT_SOURCE_VELOCITY) != 0u)
                vel += srcVel;

            // ---- particle init: identical to ParticleEmitCS ----
            GPUParticle q = (GPUParticle) 0;
            q.position = pos;
            q.velocity = vel;
            q.acceleration = e.gravity;
            q.drag = e.dragCoeff;
            q.lifetime = RandomRange(s, e.lifetimeRange.x, e.lifetimeRange.y);
            q.age = 0.0;
            q.isAlive = 1.0;
            q.startColor = RandomColor(s, e.startColorMin, e.startColorMax);
            q.endColor = RandomColor(s, e.endColorMin, e.endColorMax);
            q.color = q.startColor;
            q.startSize = RandomRange(s, e.sizeRange.x, e.sizeRange.y);
            q.endSize = RandomRange(s, e.sizeRange.z, e.sizeRange.w);
            q.size = q.startSize;
            q.rotation = RandomRange(s, e.rotationRange.x, e.rotationRange.y);
            q.angularVel = RandomRange(s, e.angularVelRange.x, e.angularVelRange.y);

            // cube: 3 axes share the same ranges (same as ParticleEmitCS)
            q.renderMode = e.renderMode;
            q.rot3.x = RandomRange(s, e.rotationRange.x, e.rotationRange.y);
            q.rot3.y = RandomRange(s, e.rotationRange.x, e.rotationRange.y);
            q.rot3.z = RandomRange(s, e.rotationRange.x, e.rotationRange.y);
            q.angVel3.x = RandomRange(s, e.angularVelRange.x, e.angularVelRange.y);
            q.angVel3.y = RandomRange(s, e.angularVelRange.x, e.angularVelRange.y);
            q.angVel3.z = RandomRange(s, e.angularVelRange.x, e.angularVelRange.y);
            q.seed = s;
            q.textureIndex = e.textureIndex;
            q.atlasRows = e.atlasRows;
            q.atlasCols = e.atlasCols;
            InitParticleFrame(e.frameMode, e.atlasIndex, e.frameCount,
                              e.atlasRows, e.atlasCols, s, q.uvFrame, q.atlasAnimate);
            q.colorKeyOffset = e.colorKeyoffset; // static region, offset 0-based
            q.colorKeyCount = e.colorKeyCount;
            q.ownerID = 0; // unowned (projectile VFX)

            particles[particleIndex] = q;
        }
    }
}