// ============================================================
// PS.hlsl - Lambert。アルベドテクスチャ * 頂点色
// + ディゾルブ（Dissolve.hlsli、DissolveCB で指定しない限り無効）
// ============================================================
#include "Common/ModelCommon.hlsli"
#include "Common/Lighting.hlsli"
#include "Common/Dissolve.hlsli"

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 glow = ApplyDissolve(input.UV); // 溶けたピクセルをクリップする

    float4 tex = albedoTexture.Sample(samplerState, input.UV);
    float3 albedo = DecodeAlbedo(tex.rgb) * input.Color.rgb;
    float3 lit = ShadeLambert(normalize(input.Normal), albedo, input.WorldPos);
    return float4(ApplyHitFlash(lit + glow), tex.a * input.Color.a);
}
