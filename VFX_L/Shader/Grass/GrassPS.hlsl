// ============================================================
// GrassPS.hlsl
// Ground color darkened at the root, lighter and warmer at the tip,
// lit like every model (Lighting.hlsli: hemisphere ambient, sun with
// its shadow, point lights, fog). The light cbuffer sits at b1 here
// because GrassCB owns b0.
// 2026-10-23: dry blades (yellower toward the tip) and the sheen of a
// blade turned over by a gust / curled by a wind source (lighter, warmer).
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
    float glow : TEXCOORD4;
    float sheen : TEXCOORD5;
    float dry : TEXCOORD6;
};

float4 main(GrassOut i) : SV_TARGET
{
    float3 albedo = i.color * lerp(g_RootColor, g_TipColor, i.t);
    albedo *= lerp(float3(1.0, 1.0, 1.0), g_DryColor.rgb, i.dry * (0.4 + 0.6 * i.t));
    albedo = lerp(albedo, albedo * g_Sheen.rgb, saturate(i.sheen * g_Sheen.a * (0.35 + 0.65 * i.t)));
    float3 lit = ShadeLambert(normalize(i.normal), albedo, i.world);
    // ember glow of burning grass: orange, brighter toward the tip (HDR -> bloom)
    lit += float3(1.0, 0.36, 0.05) * (i.glow * g_FireGlow * (0.35 + 0.65 * i.t));
    return float4(lit, 1.0);
}
