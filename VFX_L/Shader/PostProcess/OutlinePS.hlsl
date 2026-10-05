// ============================================================
// OutlinePS.hlsl
// Screen-space ink lines from the scene depth (toon look, 2026-10-04).
// Drawn as a full-screen triangle (Sky/SkyVS) after the opaque scene and
// before grass / transparent things, with a MULTIPLY blend
// (dest * src): a line darkens the color underneath toward lineTint
// instead of painting flat black.
//
// Edge test: for a plane, 1/viewZ is linear across the screen, so the
// center depth is predicted from each pair of opposite neighbours in
// 1/z. A pixel clearly IN FRONT of that prediction (relative to its own
// depth) is on a silhouette or a convex crease of the nearer surface.
// Only the nearer side gets the line (crisp, one pixel row per step).
// Planes at grazing angles predict exactly and stay clean.
// ============================================================

Texture2DMS<float> g_Depth : register(t0);

cbuffer OutlineCB : register(b0)
{
    float depthA;       // eye depth = depthB / (d - depthA) (from the projection matrix)
    float depthB;
    float thickness;    // neighbour distance in pixels (>= 1)
    float threshold;    // relative depth step for a line (0.03 = 3 % of the depth)
    float fadeStart;    // meters: lines fade out between these
    float fadeEnd;
    float strength;     // 0..1
    float debugView;    // 1 = show the eye depth as stripes (tuning)
    float3 lineTint;    // the color the line multiplies toward (linear)
    float pad1;
};

float EyeDepth(int2 p, int2 size)
{
    p = clamp(p, int2(0, 0), size - 1);
    float d = g_Depth.Load(p, 0);
    return depthB / (d - depthA);
}

float4 main(float4 pos : SV_POSITION) : SV_Target
{
    int2 size;
    int samples;
    g_Depth.GetDimensions(size.x, size.y, samples);

    int2 p = int2(pos.xy);
    float zc = EyeDepth(p, size);
    if (debugView > 0.5)
        return float4(frac(zc * 0.25), frac(zc * 0.05), 1.0, 1.0);
    int t = max(1, (int)(thickness + 0.5));

    float edge = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        int2 o = (i == 0) ? int2(1, 0) : (i == 1) ? int2(0, 1) : (i == 2) ? int2(1, 1) : int2(1, -1);
        o *= t;
        float za = EyeDepth(p + o, size);
        float zb = EyeDepth(p - o, size);
        float zp = 2.0 / (1.0 / za + 1.0 / zb);   // the neighbours' plane at the center
        float front = (zp - zc) / zc;              // > 0: the center is in front of it
        edge = max(edge, smoothstep(threshold, threshold * 2.0, front));
    }

    float fade = 1.0 - smoothstep(fadeStart, max(fadeEnd, fadeStart + 1e-3), zc);
    float k = saturate(edge * strength * fade);
    return float4(lerp(float3(1.0, 1.0, 1.0), lineTint, k), 1.0);
}
