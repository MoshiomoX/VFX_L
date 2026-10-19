// ============================================================
// SwarmTornadoEmitCS.hlsl
// Dust at the foot of every live tornado (SWARM_KIND_TORNADO, 2026-10-19).
// One thread per enemy slot: a tornado with its life running (fuse > 0)
// emits from every emitter of the TornadoDust recipe (VFXId::TornadoDust)
// at its feet, rate x g_RateMul. On top of the emitter's own shape the
// particles get a tangential kick around the axis (g_Swirl) and half of
// the tornado's travel speed, so the sand spirals up and follows it.
//
// Dispatched by SwarmSystem (Swarm/SwarmSystemTornado.cpp) right after
// the grass-fire emitter, sharing the free-pool reservation (emitBudget).
// ============================================================
#include "../Particle/Common/EmitInit.hlsli"
#define SWARM_FRAME_CB_REG b3   // not written for this dispatch; b0 is ParticleCommon's GlobalCB
#include "../Common/SwarmCommon.hlsli"

// Must match SwarmSystem::TornadoEmitCB (b2)
cbuffer SwarmTornadoEmitCB : register(b2)
{
    float g_FootY;        // enemy position -> feet (-(radius + capsule half))
    float g_RateMul;      // multiplies every emitter's rate
    uint g_DustVfx;       // recipe index of VFXId::TornadoDust (0 = none)
    float g_Swirl;        // tangential speed added around the axis, m/s
    float g_FootRadius;   // the ring the dust starts on (the hit radius), m
    uint g_SlotCount;     // kMaxEnemies
    float2 _tePad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t2);
StructuredBuffer<SwarmRecipe> recipes : register(t3);
StructuredBuffer<GPUEmitter> emitters : register(t4);
Buffer<uint> deadCount : register(t5);

RWStructuredBuffer<GPUParticle> particles : register(u0);
ConsumeStructuredBuffer<uint> deadList : register(u1);
RWByteAddressBuffer emitBudget : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_SlotCount || g_DustVfx == 0u)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    SwarmEnemyExtra ex = enemyExtra[i];
    if (ex.kind != SWARM_KIND_TORNADO || ex.fuse <= 0.0)
        return;

    SwarmRecipe r = recipes[g_DustVfx];
    if (r.particleCount == 0u)
        return;

    SwarmEnemy e = enemies[i];
    float3 base = e.position + float3(0.0, g_FootY, 0.0);
    uint seed = g_BaseSeed ^ (i * 73856093u);

    for (uint ei = 0u; ei < r.particleCount; ++ei)
    {
        GPUEmitter em = emitters[r.particleStart + ei];
        em.position = base + em.position;   // the entry position is an offset from the feet
        // the emitter shapes are authored for a 1 m tornado: scale the ring / cone to the hit radius
        em.shapeSize.xy *= g_FootRadius;

        uint k = StochasticCount(em.emitRate * g_RateMul, g_DeltaTime, seed);
        if (k == 0u)
            continue;

        // ---- reserve from the free pool (shared with the other emitters this frame) ----
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
            float3 pos = em.position;
            float3 vel = float3(0, 0, 0);
            EmitShape(em, s, pos, vel);
            // swirl around the axis + ride along with the tornado
            float2 radial = pos.xz - base.xz;
            float rl = length(radial);
            if (rl > 1e-3)
            {
                float2 tangent = float2(-radial.y, radial.x) / rl;
                vel.xz += tangent * g_Swirl;
            }
            vel += e.velocity * 0.5;
            particles[particleIndex] = InitParticleFromEmitter(em, s, pos, vel);
        }
    }
}
