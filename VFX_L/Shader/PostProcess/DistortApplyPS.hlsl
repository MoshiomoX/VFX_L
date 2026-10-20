// ============================================================
// DistortApplyPS.hlsl
// Applies the particle screen distortion to the scene BEFORE the UI is
// drawn (2026-10-20: the composite used to do it, which bent the HUD too).
// Full-screen triangle (CompositeVS) into the scene's HDR target:
// colour = resolved scene sampled at uv + offset x strength. Pixels with
// no offset come back unchanged.
// ============================================================
Texture2D sceneTex : register(t0);     // the resolved HDR scene (before this pass)
Texture2D distortTex : register(t1);   // uv offsets written by ParticleDistortPS, resolved
SamplerState linearClamp : register(s0);

cbuffer DistortApplyCB : register(b0)
{
    float g_Strength;   // multiplies the offsets
    float g_Aspect;     // width / height (same pixel distance both ways)
    float2 _pad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput i) : SV_TARGET
{
    float2 d = distortTex.Sample(linearClamp, i.uv).xy * g_Strength;
    d.y *= g_Aspect;
    float2 uv = saturate(i.uv + d);
    return float4(sceneTex.Sample(linearClamp, uv).rgb, 1.0);
}
