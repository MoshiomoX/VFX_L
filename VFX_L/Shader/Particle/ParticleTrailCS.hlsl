// ============================================================
// ParticleTrailCS.hlsl
// Runs right after ParticleUpdateCS. One thread per particle slot.
//
// For every alive particle that has a trail style:
//   - push the current position into its ring when the particle's
//     age crosses the next point of the sampling grid
//   - add it to the trail alive list (DrawInstancedIndirect args)
//
// Kept out of UpdateCS on purpose: that shader already binds 7 UAVs
// and D3D11.0 allows 8.
// ============================================================
#include "Common/ParticleCommon.hlsli"
#include "Common/ParticleTrail.hlsli"

StructuredBuffer<TrailStyle> trailStyles : register(t0);

RWStructuredBuffer<GPUParticle> particles : register(u0);
RWStructuredBuffer<float3> trailPoints : register(u1);
RWStructuredBuffer<uint> trailAlive : register(u2);
RWBuffer<uint> g_TrailArgs : register(u3); // [1] = InstanceCount

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxParticles)
        return;

    GPUParticle p = particles[i];
    if (p.isAlive < 0.5 || p.trailStyle == 0u)
        return;

    TrailStyle s = trailStyles[p.trailStyle - 1u];
    float interval = TrailInterval(s.lifetime);

    uint head = TrailHead(p.trailState);
    uint count = TrailCount(p.trailState);

    // UpdateCS already advanced age by g_DeltaTime this frame
    uint cellNow = (uint) (p.age / interval);
    uint cellPrev = (uint) (max(p.age - g_DeltaTime, 0.0) / interval);

    // count == 0: first time we see this particle -> record the birth place
    if (count == 0u || cellNow != cellPrev)
    {
        head = (count == 0u) ? 0u : (head + 1u) % TRAIL_POINTS;
        trailPoints[i * TRAIL_POINTS + head] = p.position;
        count = min(count + 1u, TRAIL_POINTS);
        particles[i].trailState = TrailPack(head, count);
    }

    uint slot;
    InterlockedAdd(g_TrailArgs[1], 1u, slot);
    trailAlive[slot] = i;
}
