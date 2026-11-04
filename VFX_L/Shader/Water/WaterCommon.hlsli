// ============================================================
// WaterCommon.hlsli
// 池の水面（World/WaterSurface、2026-10-27）: WaterVS / WaterPS が共有する
// フレームごとの cbuffer と、頂点 -> ピクセルの構造体。
// C++ 側: WaterSurface::WaterCB（288 バイト）
// ============================================================
#ifndef WATER_COMMON_HLSLI
#define WATER_COMMON_HLSLI

#define WATER_RIPPLE_SIZE 1024.0   // = WaterSurface::kRippleSize

cbuffer WaterCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float3 g_CamPos;        float g_Time;           // 秒（ポーズ中は止まる）
    float2 g_MapOrigin;     float2 g_MapSize;       // 波紋のテクスチャが覆う範囲（ワールド xz、m）
    float2 g_WindDir;       float g_WindSpeed;      float g_WindGust;    // 世界の風（天気の強さ込み）
    float g_WindScale;      float g_WindWaveSpeed;  float g_WindTime;    float g_Wave2Weight;
    float4 g_Wave2;         // 草の 2 つ目の突風の波: xy 向き、z 1 m あたりの波の数、w rad/s
    float4 g_Shallow;       // rgb、a = 岸の近くの不透明度
    float4 g_Deep;          // rgb、a = 真ん中の不透明度
    float4 g_Foam;          // rgb、a = 岸の泡の幅（m）
    float4 g_Turbid;        // rgb = 濁った色、a = 濁り具合（砂嵐）
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
    float shore : TEXCOORD1;   // 一番近い乾いたセルまでのメートル
};

#endif
