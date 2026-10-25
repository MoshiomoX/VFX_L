// ============================================================
// GrassCommon.hlsli
// Shared by the grass shaders (Graphics/Renderer/GrassRenderer):
//   GrassCB     per-frame cbuffer, b0 in every stage
//   GrassBlade  one blade, written by GrassCullCS, read by GrassVS.
//               Since 2026-10-23 the cull CS also animates it once per
//               blade (wind, wind sources, trample, fire) and stores the
//               spine as a cubic Bezier; the VS only builds the 21 vertices
//   GrassHash   places blades on a world-aligned lattice (a blade
//               stays put while the camera moves)
// C++ mirror: GrassRenderer::GrassCB / GrassBlade
// ============================================================
#ifndef GRASS_COMMON_HLSLI
#define GRASS_COMMON_HLSLI

#define GRASS_MAX_GUSTS 4   // = GrassRenderer::kMaxGusts

cbuffer GrassCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float4 g_Frustum[6];    // inward planes: inside = dot(xyz, p) + w >= 0
    float3 g_CamPos;
    float g_Time;           // seconds (wind). Stops while the game is paused
    float2 g_MapOrigin;     // world xz of the map's min corner
    float2 g_MapSize;       // map size in meters
    int2 g_CellMin;         // first lattice cell of this frame's window
    uint2 g_CellCount;      // window size in lattice cells
    float g_Spacing;        // lattice spacing, meters
    float g_MaxDist;        // no blades beyond this distance from the camera
    float g_FullDist;       // full density up to here
    float g_FarDensity;     // density at g_MaxDist, 0..1
    float g_HeightMin;
    float g_HeightMax;
    float g_WidthMin;
    float g_WidthMax;
    float g_CurlMax;        // own curl: tip offset / height
    float g_MaxSlope;       // steeper ground (rise / run) grows nothing (cliff faces)
    float g_WindStrength;   // tip offset / height in a full gust
    float g_WindSpeed;      // gust waves, radians per second
    float2 g_WindDir;       // normalized xz
    float g_WindScale;      // gust waves per meter
    float g_TrampleBend;    // tip offset / height where fully trampled
    float3 g_RootColor;     // multiplies the ground color at the root
    float g_TrampleRadius;  // the player's disc, 0 = not touching the grass
    float3 g_TipColor;      // multiplies the ground color at the tip
    float g_TrampleDecay;   // this frame's multiplier of the old trample, exp(-dt / recover)
    float3 g_Player;
    uint g_MaxBlades;       // capacity of the blade buffer (GrassCullCS keeps at most this many)
    // burnt grass (GrassFireField, 2026-10-12): g_Fire = (temperature, fuel 0..1)
    float g_FireIgnite;     // temperature where the grass burns (ember glow starts a bit below)
    float g_FireShrink;     // height lost where the fuel is gone
    float g_FireGlow;       // HDR brightness of the ember glow at the tip
    float g_FireOn;         // 0 = no fire map bound, skip the lookup
    // gusts (the Gust spell 2026-10-12, boss slams / rock rain): this frame's push stamps on the trample map,
    // up to GRASS_MAX_GUSTS per frame (2026-10-25: several rocks landing in one frame each press their own disc).
    // g_GustA = (root.x, root.z, dir.x, dir.z), g_GustB = (length, radius, 0, 0). radius 0 = unused entry.
    // length 0 = an outward disc around the root; otherwise a fan from the root along dir, length m long (2026-10-13)
    float4 g_GustA[GRASS_MAX_GUSTS];
    float4 g_GustB[GRASS_MAX_GUSTS];
    // environment stamps from the GPU swarm (2026-10-13): x = explosion radius multiplier (area radius x this is
    // flattened outward), y = beam band half width (x the beam radius), z = enemy footprint strength (0 = off),
    // w = enemy footprint radius m
    float4 g_TrampleExtra;
    // ---- finer grass (2026-10-23, the user: "a bit more detail, rolled up by the wind") ----
    float4 g_Wave2;         // a second, smaller gust wave: xy = direction, z = waves per meter, w = radians per second
    float4 g_Sheen;         // rgb = multiplies the colour where a blade is turned over (gust / vortex), a = amount (0 = off)
    float4 g_Vortex;        // wind sources: x = air speed (m/s) that lays a blade along the flow, y = updraft (m/s) that
                            // curls the tip up toward the core, z = flutter (tip offset / height), w = extra twist (rad) when curled
    float4 g_BladeShape;    // x = own twist (+- rad), y = share of dry blades 0..1, z = weight of the second wave 0..1,
                            // w = wind-source trail on the trample map (strength, 0 = off)
    float4 g_DryColor;      // rgb = multiplies a dry blade, a = air speed (m/s) for the strongest trail stamp
};

// one blade (96 bytes), animated by GrassCullCS. The spine is a cubic Bezier from the
// root (0) through c1, c2 to tip, all relative to the root
struct GrassBlade
{
    float3 pos;     // root, on the ground
    float height;   // meters (after the distance thinning and the fire shrink)
    float3 c1;
    float width;    // meters at the root
    float3 c2;
    float twist;    // the width axis turns this many radians from root to tip
    float3 tip;
    float sheen;    // 0..1 the lighter underside shows (a gust turned it over, a vortex curls it)
    float2 side;    // width axis at the root (unit xz)
    float shade;    // brightness variation
    float dry;      // 0..1 a dry, yellow blade
    float burn;     // 0..1 charred (fire)
    float glow;     // 0..1 ember glow at the tip (fire)
    float2 pad;
};

uint GrassHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// next random number 0..1 from the running seed
float GrassRand(inout uint s)
{
    s = GrassHash(s + 0x9e3779b9u);
    return (float) (s & 0xFFFFFFu) / 16777216.0;
}

#endif
