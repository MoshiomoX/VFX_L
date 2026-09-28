// ============================================================
// SwarmOrbPS.hlsl
// Exp orb gem: self-lit crystal. Linear HDR out, bloom picks up
// whatever goes over its threshold.
//
//   body  : vertex color x g_Emissive, the flat faces shaded a little
//           by the sun (g_Facet) so the spinning gem flickers
//   glint : sharp sun highlight on the faces
//   rim   : fresnel edge in g_RimColor
// The whole thing is scaled by Color.a (SwarmOrbVS: brighter while
// pulled) and fogged like every other lit surface.
//
// b0 = LightBuffer (sun direction, camera, fog), b1 = our tuning.
// ============================================================
#include "../Common/ModelCommon.hlsli"
#include "../Common/Lighting.hlsli"

// Must match SwarmSystem::OrbShadeCB
cbuffer SwarmOrbShadeCB : register(b1)
{
    float g_Emissive;     // body brightness
    float g_Facet;        // 0 = flat glow, 1 = faces fully sun-shaded
    float g_RimGain;
    float g_RimPower;

    float g_GlintGain;
    float g_GlintPower;
    float2 _shadePad;

    float4 g_RimColor;    // linear rgb
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 N = normalize(input.Normal);
    float3 V = normalize(cameraPosition - input.WorldPos);
    float3 L = normalize(-dirLight.direction);

    float wrap = saturate(dot(N, L) * 0.5 + 0.5);
    float3 body = input.Color.rgb * g_Emissive * lerp(1.0 - g_Facet, 1.0, wrap);

    float3 H = normalize(L + V);
    float glint = pow(saturate(dot(N, H)), max(g_GlintPower, 1.0)) * g_GlintGain;

    float rim = pow(1.0 - saturate(dot(N, V)), max(g_RimPower, 0.1)) * g_RimGain;

    float3 c = (body + glint * dirLight.color + rim * g_RimColor.rgb) * input.Color.a;
    return float4(ApplyFog(c, input.WorldPos), 1.0);
}
