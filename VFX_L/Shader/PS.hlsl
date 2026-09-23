// ============================================================
// PS.hlsl - Lambert. albedo texture * vertex color
// + dissolve (Dissolve.hlsli, off unless DissolveCB says otherwise)
// ============================================================
#include "Common/ModelCommon.hlsli"
#include "Common/Lighting.hlsli"
#include "Common/Dissolve.hlsli"

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 glow = ApplyDissolve(input.UV); // clips dissolved pixels

    float4 tex = albedoTexture.Sample(samplerState, input.UV);
    float3 albedo = tex.rgb * input.Color.rgb;
    float3 lit = ShadeLambert(normalize(input.Normal), albedo);
    return float4(lit + glow, tex.a * input.Color.a);
}
