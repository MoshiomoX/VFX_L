// ============================================================
// PBR_PS.hlsl - Cook-Torrance. Linear HDR out, no tonemap here
// + dissolve (Dissolve.hlsli, off unless DissolveCB says otherwise)
// ============================================================
#include "Common/ModelCommon.hlsli"
#include "Common/Lighting.hlsli"
#include "Common/Dissolve.hlsli"

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 glow = ApplyDissolve(input.UV); // clips dissolved pixels

    float3 albedo = albedoTexture.Sample(samplerState, input.UV).rgb;
    float3 nm = normalTexture.Sample(samplerState, input.UV).rgb;
    float metallic = metallicTexture.Sample(samplerState, input.UV).b;
    float roughness = roughnessTexture.Sample(samplerState, input.UV).g;
    float ao = aoTexture.Sample(samplerState, input.UV).r;

    float3 N = PerturbNormal(input.Normal, input.Tangent, nm);
    float3 V = normalize(cameraPosition - input.WorldPos);

    float3 c = ShadePBR(N, V, albedo, metallic, roughness, ao, input.WorldPos);
    return float4(c * input.Color.rgb + glow, 1.0);
}
