// ============================================================
// PS.hlsl - Lambert. albedo texture * vertex color
// ============================================================
#include "Common/ModelCommon.hlsli"
#include "Common/Lighting.hlsli"

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 tex = albedoTexture.Sample(samplerState, input.UV);
    float3 albedo = tex.rgb * input.Color.rgb;
    float3 lit = ShadeLambert(normalize(input.Normal), albedo);
    return float4(lit, tex.a * input.Color.a);
}