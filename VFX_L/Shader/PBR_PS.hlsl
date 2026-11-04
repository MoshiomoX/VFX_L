// ============================================================
// PBR_PS.hlsl - Cook-Torrance。リニア HDR で出力、トーンマップはここでしない
// + ディゾルブ（Dissolve.hlsli、DissolveCB で指定しない限りオフ）
// ============================================================
#include "Common/ModelCommon.hlsli"
#include "Common/Lighting.hlsli"
#include "Common/Dissolve.hlsli"

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 glow = ApplyDissolve(input.UV); // 溶けたピクセルは clip する

    float3 albedo = DecodeAlbedo(albedoTexture.Sample(samplerState, input.UV).rgb);
    float3 nm = normalTexture.Sample(samplerState, input.UV).rgb;
    float metallic = metallicTexture.Sample(samplerState, input.UV).b;
    float roughness = roughnessTexture.Sample(samplerState, input.UV).g;
    float ao = aoTexture.Sample(samplerState, input.UV).r;

    float3 N = PerturbNormal(input.Normal, input.Tangent, nm);
    float3 V = normalize(cameraPosition - input.WorldPos);

    float3 c = ShadePBR(N, V, albedo, metallic, roughness, ao, input.WorldPos);
    return float4(ApplyHitFlash(c * input.Color.rgb + glow), 1.0);
}
