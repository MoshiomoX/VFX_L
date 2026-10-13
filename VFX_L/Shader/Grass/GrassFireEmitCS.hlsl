// ============================================================
// GrassFireEmitCS.hlsl
// Particles from burning grass (World/GrassFireField, 2026-10-12).
// One thread per texel of the fire map. A burning texel (temperature
// over g_Ignite, fuel left, grass grows here) near the camera emits
// from every emitter of the GrassFire recipe (VFXId::GrassFire) at the
// ground height, rate x intensity x g_RateMul.
//
// Dispatched by SwarmSystem (Swarm/SwarmSystemFire.cpp) right after the
// projectile / area emitters so the free-pool reservation (emitBudget)
// is shared: the fire never over-consumes the dead list.
// ============================================================
#include "../Particle/Common/EmitInit.hlsli"

// Must match SwarmSystem::FireEmitCB (b2; b0 / b1 are ParticleCommon's)
cbuffer GrassFireEmitCB : register(b2)
{
    float2 g_MapOrigin;
    float2 g_MapSize;
    float3 g_CamPos;
    float g_EmitRadius;     // texels farther than this from the camera emit nothing
    float g_Ignite;         // burning = temperature above this (same as GrassFireCS)
    float g_RateMul;        // multiplies every emitter's rate (per burning texel)
    uint g_FireVfx;         // recipe index of VFXId::GrassFire
    float g_HeightOffset;   // emit this much above the ground
};

// = SwarmRecipe (Shader/Common/SwarmCommon.hlsli), 32 bytes
struct FireRecipe
{
    uint particleStart;
    uint particleCount;
    uint modelStart;
    uint modelCount;
    uint lightStart;
    uint lightCount;
    uint spriteStart;
    uint spriteCount;
};

Texture2D<float2> fireMap : register(t0);        // (temperature, fuel)
Texture2D<float4> groundMap : register(t1);      // a = grass
StructuredBuffer<FireRecipe> recipes : register(t2);
StructuredBuffer<GPUEmitter> emitters : register(t3);
Buffer<uint> deadCount : register(t4);
Texture2D<float> heightMap : register(t5);       // GridWorld heights (GrassRenderer's texture)

RWStructuredBuffer<GPUParticle> particles : register(u0);
ConsumeStructuredBuffer<uint> deadList : register(u1);
RWByteAddressBuffer emitBudget : register(u2);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    fireMap.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;

    float2 f = fireMap.Load(int3(id.xy, 0));
    if (f.x <= g_Ignite || f.y <= 0.001)
        return;

    float2 uv = (float2(id.xy) + 0.5) / float2(w, h);
    float2 xz = g_MapOrigin + uv * g_MapSize;
    if (distance(xz, g_CamPos.xz) > g_EmitRadius)
        return;

    uint gw, gh;
    groundMap.GetDimensions(gw, gh);
    if (groundMap.Load(int3(int2(uv * float2(gw, gh)), 0)).a < 0.5)
        return;

    FireRecipe r = recipes[g_FireVfx];
    if (r.particleCount == 0u)
        return;

    // fresh fire burns hard, a nearly spent texel only smoulders
    float intensity = saturate((f.x - g_Ignite) * 2.0) * saturate(f.y * 4.0);
    if (intensity <= 0.0)
        return;

    uint hw, hh;
    heightMap.GetDimensions(hw, hh);
    float groundY = heightMap.Load(int3(int2(uv * float2(hw, hh)), 0));
    float3 base = float3(xz.x, groundY + g_HeightOffset, xz.y);

    uint seed = g_BaseSeed ^ (id.x * 73856093u) ^ (id.y * 19349663u);

    for (uint ei = 0u; ei < r.particleCount; ++ei)
    {
        GPUEmitter e = emitters[r.particleStart + ei];
        e.position = base + e.position;   // the entry position is an offset from the ground

        uint k = StochasticCount(e.emitRate * g_RateMul * intensity, g_DeltaTime, seed);
        if (k == 0u)
            continue;

        // ---- reserve from the free pool (shared with SwarmEmitCS this frame) ----
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
            particles[particleIndex] = InitParticleFromEmitter(e, s, pos, vel);
        }
    }
}
