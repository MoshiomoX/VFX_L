// ============================================================
// OutlinePS.hlsl
// Screen-space ink lines from the scene depth (toon look, 2026-10-04)
// + faction lines from the stencil (2026-10-06: friends blue and a bit
// thicker, enemies red).
// Drawn as a full-screen triangle (Sky/SkyVS) after the opaque scene and
// before grass / transparent things, with a PREMULTIPLIED ALPHA blend
// (out + dest * (1 - a)): the color underneath is pulled toward the line
// color by k. With the dark default tint this looks like the old multiply.
//
// Edge test: for a plane, 1/viewZ is linear across the screen, so the
// center depth is predicted from each pair of opposite neighbours in
// 1/z. A pixel clearly IN FRONT of that prediction (relative to its own
// depth) is on a silhouette or a convex crease of the nearer surface.
// Only the nearer side gets the line (crisp, one pixel row per step).
//
// Faction: the stencil holds 1 (friend) / 2 (enemy) where those were drawn
// (RenderSystem / SwarmSystem write it). A pixel with a faction gets that
// faction's color and thickness, and is also a line wherever a neighbour
// has a different stencil (so the whole silhouette is closed even where
// the depth step is small, e.g. the feet on the ground).
// ============================================================

Texture2DMS<float> g_Depth : register(t0);
Texture2DMS<uint2> g_Stencil : register(t1);   // X24_TYPELESS_G8_UINT: stencil in .g

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
    float3 lineTint;    // the color of the neutral line (linear)
    float factionStrength;   // 0 = faction lines off
    float3 friendColor;
    float friendThickness;   // pixels
    float3 enemyColor;
    float enemyThickness;
};

float EyeDepth(int2 p, int2 size)
{
    p = clamp(p, int2(0, 0), size - 1);
    float d = g_Depth.Load(p, 0);
    return depthB / (d - depthA);
}

uint Faction(int2 p, int2 size)
{
    p = clamp(p, int2(0, 0), size - 1);
    return g_Stencil.Load(p, 0).g;
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

    // ---- which line: neutral, friend or enemy (by the stencil under this pixel) ----
    uint f = (factionStrength > 0.0) ? Faction(p, size) : 0u;
    float3 color = lineTint;
    float  px = thickness;
    if (f == 1u) { color = friendColor; px = friendThickness; }
    else if (f == 2u) { color = enemyColor; px = enemyThickness; }
    int t = max(1, (int)(px + 0.5));

    float edge = 0.0;
    float fedge = 0.0;
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
        if (f != 0u)
        {
            // silhouette of a faction unit: a neighbour that is not the same faction
            if (Faction(p + o, size) != f || Faction(p - o, size) != f) fedge = 1.0;
        }
    }

    float k;
    if (f != 0u)
    {
        // faction lines do not fade with distance (a far enemy still reads as red)
        k = saturate(max(edge, fedge) * factionStrength);
    }
    else
    {
        float fade = 1.0 - smoothstep(fadeStart, max(fadeEnd, fadeStart + 1e-3), zc);
        k = saturate(edge * strength * fade);
    }
    return float4(color * k, k);
}