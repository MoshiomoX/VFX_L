// ============================================================
// WaterCommon.hlsli
// The pond water surface (World/WaterSurface, 2026-10-27): the per-frame
// cbuffer shared by WaterVS / WaterPS and the vertex -> pixel struct.
// C++ mirror: WaterSurface::WaterCB (288 bytes)
// ============================================================
#ifndef WATER_COMMON_HLSLI
#define WATER_COMMON_HLSLI

#define WATER_RIPPLE_SIZE 1024.0   // = WaterSurface::kRippleSize

cbuffer WaterCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float3 g_CamPos;        float g_Time;           // seconds (stops while paused)
    float2 g_MapOrigin;     float2 g_MapSize;       // the ripple texture covers this (world xz, m)
    float2 g_WindDir;       float g_WindSpeed;      float g_WindGust;    // world wind (with the weather strength)
    float g_WindScale;      float g_WindWaveSpeed;  float g_WindTime;    float g_Wave2Weight;
    float4 g_Wave2;         // the grass's second gust wave: xy dir, z waves per m, w rad/s
    float4 g_Shallow;       // rgb, a = opacity near the shore
    float4 g_Deep;          // rgb, a = opacity in the middle
    float4 g_Foam;          // rgb, a = shore foam width (m)
    float4 g_Turbid;        // rgb = muddy color, a = how muddy (sandstorm)
    float g_DeepDist;       float g_CalmWind;       float g_FullWind;    float g_WaveNormal;
    float g_WaveScale;      float g_WaveFlow;       float g_RippleNormal; float g_RippleCrest;
    float g_SkyReflect;     float g_SpecStrength;   float g_WindDarken;  float g_HasSources;
    float4 g_SkyZenith;     // rgb
    float4 g_SkyHorizon;    // rgb
};

struct WaterOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float shore : TEXCOORD1;   // meters to the nearest dry cell
};

#endif
