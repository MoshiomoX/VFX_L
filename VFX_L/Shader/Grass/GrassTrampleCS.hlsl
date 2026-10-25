// ============================================================
// GrassTrampleCS.hlsl
// The trample map: one texel per small patch of the whole map,
// xy = the direction the grass is pushed, length = how hard (0..1).
// Every frame the old value fades (g_TrampleDecay) and the stamps
// push it: the player's disc (away from the centre), the Gust spell
// (disc or fan), and the live GPU areas that shake the camera
// (2026-10-13: explosions flatten a disc outward, the beam capsule
// bends a band sideways). The stronger push wins.
// Enemies' feet are stamped afterwards by GrassStampCS (scatter).
// 2026-10-23: the wind sources (twisters, the vortex, sand tornadoes,
// gusts) press the grass along their air flow, so a passing twister
// leaves a swirled trail that stands back up like a footprint.
// Ping-pong: reads last frame's map, writes the other one (typed UAV
// loads of R16G16_FLOAT are not guaranteed on D3D11).
// ============================================================
#include "GrassCommon.hlsli"
#include "../Common/WindSources.hlsli"

// = SwarmArea (Shader/Common/SwarmCommon.hlsli), 48 bytes. Only center / radius / flags are read
struct TrampleArea
{
    float3 center;
    float radius;
    float damage;
    float timeLeft;
    float tickInterval;
    float tickTimer;
    float halfHeight;
    uint flags;
    uint vfxType;
    uint tickNow;
};
#define TRAMPLE_MAX_AREAS 256u   // = SWARM_MAX_AREAS
#define TRAMPLE_AREA_CAPSULE 4u  // SWARM_AREA_CAPSULE (beam)
#define TRAMPLE_AREA_SHAKE 8u    // SWARM_AREA_SHAKE (explosions and the beam)
#define TRAMPLE_MAX_SOURCES 32u

Texture2D<float2> g_TrampleIn : register(t0);
StructuredBuffer<TrampleArea> g_Areas : register(t1);
Buffer<uint> g_AreaStates : register(t2);          // 0 = dead
StructuredBuffer<float4> g_AreaEnds : register(t3); // capsule end per slot (AreaTickCS)
StructuredBuffer<WindSource> g_WindSources : register(t4);   // unbound = no sources (radius 0)
RWTexture2D<float2> g_TrampleOut : register(u0);

groupshared float4 gs_SrcA[TRAMPLE_MAX_SOURCES];   // disc: (cx, cz, 0, 0). capsule: (sx, sz, ex, ez)
groupshared float2 gs_SrcB[TRAMPLE_MAX_SOURCES];   // (radius, 0 = disc / 1 = capsule)
groupshared uint gs_SrcCount;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex, uint3 gid : SV_GroupID)
{
    uint w, h;
    g_TrampleOut.GetDimensions(w, h);

    // ---- compact the live shaking areas into shared memory (64 threads, 4 areas each) ----
    // 2026-10-25: only the ones that can reach this group's 8 x 8 texels (a box test on the texel
    // centres; the push of a source outside it is 0 for every texel here, so the result is the same,
    // and many explosions no longer fill the 32 slots with far-away ones)
    float2 texelSize = g_MapSize / float2(w, h);
    float2 boxMin = g_MapOrigin + (float2(gid.xy * 8u) + 0.5) * texelSize;
    float2 boxMax = boxMin + 7.0 * texelSize;
    if (gi == 0u)
        gs_SrcCount = 0u;
    GroupMemoryBarrierWithGroupSync();
    if (g_TrampleExtra.x > 0.0 || g_TrampleExtra.y > 0.0)
    {
        for (uint a = gi; a < TRAMPLE_MAX_AREAS; a += 64u)
        {
            if (g_AreaStates[a] == 0u)
                continue;
            TrampleArea ar = g_Areas[a];
            if ((ar.flags & TRAMPLE_AREA_SHAKE) == 0u)
                continue;
            bool capsule = (ar.flags & TRAMPLE_AREA_CAPSULE) != 0u;
            float r = ar.radius * (capsule ? g_TrampleExtra.y : g_TrampleExtra.x);
            if (r <= 0.0)
                continue;
            float4 e = g_AreaEnds[a];
            float reach = r + 0.01;   // a little margin (the texel centres here and below are computed differently)
            float2 lo = (capsule ? min(ar.center.xz, e.xz) : ar.center.xz) - reach;
            float2 hi = (capsule ? max(ar.center.xz, e.xz) : ar.center.xz) + reach;
            if (any(hi < boxMin) || any(lo > boxMax))
                continue;
            uint slot;
            InterlockedAdd(gs_SrcCount, 1u, slot);
            if (slot < TRAMPLE_MAX_SOURCES)
            {
                gs_SrcA[slot] = capsule ? float4(ar.center.xz, e.xz) : float4(ar.center.xz, 0.0, 0.0);
                gs_SrcB[slot] = float2(r, capsule ? 1.0 : 0.0);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (id.x >= w || id.y >= h)
        return;

    float2 v = g_TrampleIn.Load(int3(id.xy, 0)) * g_TrampleDecay;
    float2 xz = g_MapOrigin + (float2(id.xy) + 0.5) / float2(w, h) * g_MapSize;
    // this frame's gusts (up to GRASS_MAX_GUSTS, radius 0 = unused entry)
    [unroll]
    for (uint gk = 0u; gk < GRASS_MAX_GUSTS; ++gk)
    {
        float2 gRoot = g_GustA[gk].xy;
        float2 gDir = g_GustA[gk].zw;
        float gLength = g_GustB[gk].x;
        float gRadius = g_GustB[gk].y;
        // a gust: one frame's stamp, the whole disc flattened outward (stands back up like a footprint)
        if (gRadius > 0.0 && gLength <= 0.0)
        {
            float2 d = xz - gRoot;
            float dist = length(d);
            if (dist < gRadius && dist > 1e-3)
            {
                float s = 0.75 + 0.25 * sqrt(1.0 - dist / gRadius);
                float2 push = d / dist * s;
                if (dot(push, push) > dot(v, v))
                    v = push;
            }
        }
        // a forward gust: a fan from the caster along gDir. Narrow at the root (0.35 r), full
        // radius r at the far end; the grass falls downwind, bending a little outward at the sides
        if (gRadius > 0.0 && gLength > 0.0)
        {
            float2 d = xz - gRoot;
            float along = dot(d, gDir);
            if (along > -0.3 && along < gLength)
            {
                float2 sideV = d - gDir * along;
                float side = length(sideV);
                float t = saturate(along / gLength);
                float r = gRadius * lerp(0.35, 1.0, t);
                if (side < r)
                {
                    float s = 0.75 + 0.25 * sqrt(1.0 - side / r);
                    float2 dir = gDir;
                    if (side > 1e-3)
                        dir = normalize(gDir + sideV / side * (0.4 * side / r));
                    float2 push = dir * s;
                    if (dot(push, push) > dot(v, v))
                        v = push;
                }
            }
        }
    }
    // explosions (disc, flattened outward while the area lives) and the beam (a band pushed sideways)
    uint n = min(gs_SrcCount, TRAMPLE_MAX_SOURCES);
    for (uint k = 0u; k < n; ++k)
    {
        float4 A = gs_SrcA[k];
        float r = gs_SrcB[k].x;
        float2 push = 0.0;
        if (gs_SrcB[k].y > 0.5)
        {
            float2 seg = A.zw - A.xy;
            float t = saturate(dot(xz - A.xy, seg) / max(dot(seg, seg), 1e-4));
            float2 d = xz - (A.xy + seg * t);
            float side = length(d);
            if (side < r && side > 1e-3)
                push = d / side * (0.55 + 0.35 * sqrt(1.0 - side / r));
        }
        else
        {
            float2 d = xz - A.xy;
            float dist = length(d);
            if (dist < r && dist > 1e-3)
                push = d / dist * (0.8 + 0.2 * sqrt(1.0 - dist / r));
        }
        if (dot(push, push) > dot(v, v))
            v = push;
    }
    // the wind sources' trail: pressed along the air flow (a swirl around a twister)
    if (g_BladeShape.w > 0.0)
    {
        float lift;
        float3 air = WindSourceAir(g_WindSources, xz, lift);
        float s = length(air.xz);
        if (s > 0.5)
        {
            float2 push = air.xz / s * (saturate(s / max(g_DryColor.a, 0.1)) * g_BladeShape.w);
            if (dot(push, push) > dot(v, v))
                v = push;
        }
    }
    if (g_TrampleRadius > 0.0)
    {
        float2 d = xz - g_Player.xz;
        float dist = length(d);
        if (dist < g_TrampleRadius)
        {
            // flat-topped falloff: the whole disc is pressed, the edge softly
            float s = sqrt(1.0 - dist / g_TrampleRadius);
            float2 push = (dist > 1e-3) ? d / dist * s : float2(0.0, 0.0);
            if (dot(push, push) > dot(v, v))
                v = push;
        }
    }
    g_TrampleOut[id.xy] = v;
}
