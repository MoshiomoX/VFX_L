// ============================================================
// SkinnedPS.hlsl
// Lambert from LightBuffer (was a hard-coded light before).
// No texture yet: flat gray albedo, same look as before.
// ============================================================
#include "../Common/ModelCommon.hlsli"
#include "../Common/Lighting.hlsli"

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 albedo = float3(0.7, 0.7, 0.7);
    float3 lit = ShadeLambert(normalize(input.Normal), albedo, input.WorldPos);
    return float4(lit, 1.0);
}