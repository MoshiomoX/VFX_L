// ============================================================
// ParticleTrail.hlsli
// Per-particle ribbon trails, fully on the GPU.
//
// Every particle owns a fixed ring of TRAIL_POINTS positions:
//     trailPoints[particleIndex * TRAIL_POINTS + slot]
// Fixed mapping = no allocator, nothing to leak. A particle only
// records / draws when GPUParticle.trailStyle != 0.
//
// GPUParticle.trailStyle : 0 = no trail, otherwise style table index + 1
// GPUParticle.trailState : bits 0-7  = ring write position (newest sample)
//                          bits 8-15 = number of valid samples
//
// The ring is sampled on a time grid of lifetime / (TRAIL_POINTS - 1),
// so the ribbon always covers "the last <lifetime> seconds" no matter
// the frame rate. The live head (current position) is not stored; the
// VS reads it from the particle.
//
// Must match ParticleTrailStyleGPU / kTrailPoints in GPUParticle.h
// ============================================================
#ifndef PARTICLE_TRAIL_HLSLI
#define PARTICLE_TRAIL_HLSLI

#define TRAIL_POINTS 16u

#define TRAIL_FLAG_INHERIT_COLOR 1u
#define TRAIL_FLAG_INHERIT_SIZE  2u

struct TrailStyle
{
    float4 colorHead;
    float4 colorTail;

    float widthHead;
    float widthTail;
    float lifetime; // seconds of path the ribbon covers
    float intensity; // HDR gain on rgb

    uint flags;
    float softEdge; // 0 = hard edge, 1 = fades from the centre line
    float uvRepeat; // texture repeats along the ribbon
    float uvScroll; // U per second
};

float TrailInterval(float lifetime)
{
    return max(lifetime, 0.01) / (float) (TRAIL_POINTS - 1u);
}

uint TrailHead(uint state)
{
    return state & 0xFFu;
}

uint TrailCount(uint state)
{
    return (state >> 8u) & 0xFFu;
}

uint TrailPack(uint head, uint count)
{
    return (head & 0xFFu) | ((count & 0xFFu) << 8u);
}

#endif
