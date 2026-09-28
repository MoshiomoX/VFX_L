// ============================================================
// GrassCullCS.hlsl
// One thread per lattice cell in a square window around the camera.
// Each cell may grow one blade (hash-jittered inside the cell). Kept
// blades go to an append buffer that GrassVS draws with
// DrawInstancedIndirect.
//
// Dropped: beyond g_MaxDist, thinned out with distance (the kept ones
// get wider; the ones about to be thinned shrink first so nothing
// pops), no-grass cells (dirt ramps, the rim, unreachable plateaus:
// alpha of the ground texture), steep ground (cliff faces, from the
// height field), outside the view frustum.
// ============================================================
#include "GrassCommon.hlsli"

Texture2D<float> g_Height : register(t0);  // GridWorld heights, texel centre = height cell centre
Texture2D<float4> g_Ground : register(t1); // rgb = ground color (linear), a = 1 grass / 0 none
SamplerState s_Linear : register(s0);      // clamp
AppendStructuredBuffer<GrassBlade> g_OutBlades : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_CellCount.x || id.y >= g_CellCount.y)
        return;

    int2 cell = g_CellMin + int2(id.xy);
    uint seed = GrassHash(asuint(cell.x) * 73856093u ^ GrassHash(asuint(cell.y) * 19349663u));
    float jx = GrassRand(seed);
    float jz = GrassRand(seed);
    float2 xz = (float2(cell) + float2(jx, jz)) * g_Spacing;

    float dist = distance(xz, g_CamPos.xz);
    float density = lerp(1.0, g_FarDensity, saturate((dist - g_FullDist) / max(g_MaxDist - g_FullDist, 1e-3)));
    float keep = GrassRand(seed);
    float2 uv = (xz - g_MapOrigin) / g_MapSize;
    if (dist > g_MaxDist || keep > density || any(uv <= 0.0) || any(uv >= 1.0))
        return;

    uint tw, th;
    g_Ground.GetDimensions(tw, th);
    float4 ground = g_Ground.Load(int3(int2(uv * float2(tw, th)), 0));
    if (ground.a < 0.5)
        return;

    // steepness from the bilinear height 0.3m around (cliff edges are one height texel wide)
    float y = g_Height.SampleLevel(s_Linear, uv, 0);
    const float e = 0.3;
    float2 du = float2(e / g_MapSize.x, 0.0);
    float2 dv = float2(0.0, e / g_MapSize.y);
    float hx0 = g_Height.SampleLevel(s_Linear, uv - du, 0);
    float hx1 = g_Height.SampleLevel(s_Linear, uv + du, 0);
    float hz0 = g_Height.SampleLevel(s_Linear, uv - dv, 0);
    float hz1 = g_Height.SampleLevel(s_Linear, uv + dv, 0);
    float rise = max(max(abs(hx1 - y), abs(y - hx0)), max(abs(hz1 - y), abs(y - hz0)));
    if (rise > g_MaxSlope * e)
        return;

    GrassBlade b;
    b.pos = float3(xz.x, y, xz.y);
    b.height = lerp(g_HeightMin, g_HeightMax, GrassRand(seed)) * saturate((density - keep) / 0.08);
    b.yaw = GrassRand(seed) * 6.2831853;
    b.width = lerp(g_WidthMin, g_WidthMax, GrassRand(seed)) * min(rsqrt(max(density, 0.05)), 3.0);
    b.curl = GrassRand(seed) * g_CurlMax;
    b.shade = 0.85 + 0.3 * GrassRand(seed);

    // a sphere around the blade (the tip bends at most one height sideways)
    float3 c = b.pos + float3(0.0, b.height * 0.5, 0.0);
    float r = b.height + b.width;
    [unroll]
    for (uint i = 0u; i < 6u; ++i)
    {
        if (dot(g_Frustum[i].xyz, c) + g_Frustum[i].w < -r)
            return;
    }
    g_OutBlades.Append(b);
}
