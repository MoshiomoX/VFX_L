// ============================================================
// GrassCommon.hlsli
// Shared by the grass shaders (Graphics/Renderer/GrassRenderer):
//   GrassCB     per-frame cbuffer, b0 in every stage
//   GrassBlade  one blade, written by GrassCullCS, read by GrassVS
//   GrassHash   places blades on a world-aligned lattice (a blade
//               stays put while the camera moves)
// C++ mirror: GrassRenderer::GrassCB / GrassBlade
// ============================================================
#ifndef GRASS_COMMON_HLSLI
#define GRASS_COMMON_HLSLI

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
    uint g_MaxBlades;
};

struct GrassBlade
{
    float3 pos;     // root, on the ground
    float height;   // meters (shrinks near the distance thinning, no popping)
    float yaw;      // radians
    float width;    // meters at the root
    float curl;     // own curl, tip offset / height
    float shade;    // brightness variation
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
