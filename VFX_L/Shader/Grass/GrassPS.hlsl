// ============================================================
// GrassPS.hlsl
// Ground color darkened at the root, lighter and warmer at the tip,
// lit like every model (Lighting.hlsli: hemisphere ambient, sun with
// its shadow, point lights, fog). The light cbuffer sits at b1 here
// because GrassCB owns b0.
// ============================================================
#define MODEL_LIGHT_CB_REG b1
#include "../Common/Lighting.hlsli"
#include "GrassCommon.hlsli"

struct GrassOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 color : TEXCOORD2;
    float t : TEXCOORD3;
};

float4 main(GrassOut i) : SV_TARGET
{
    float3 albedo = i.color * lerp(g_RootColor, g_TipColor, i.t);
    float3 lit = ShadeLambert(normalize(i.normal), albedo, i.world);
    return float4(lit, 1.0);
}
