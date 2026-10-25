// ============================================================
// CompositePS.hlsl
// scene + bloom -> exposure -> (tonemap) -> gamma -> backbuffer.
// The only place in the pipeline that leaves linear HDR.
// ============================================================
Texture2D sceneTex : register(t0);
Texture2D bloomTex : register(t1);
// screen distortion (2026-10-20): uv offset written by the particles (ParticleDistortPS), resolved.
// Fallback only: normally Graphics::ApplyDistortion (DistortApplyPS) already bends the scene before the
// UI is drawn and clears the flag, so g_DistortStrength is 0 here. Only when DistortApplyPS failed to
// load does this path bend the image (and then the UI drawn into the HDR target bends with it)
Texture2D distortTex : register(t2);
SamplerState linearClamp : register(s0);

cbuffer CompositeCB : register(b0)
{
    float g_BloomIntensity;
    float g_Exposure;
    uint g_Tonemap; // 0 = off, 1 = ACES
    float g_Gamma; // display gamma exponent (2.2 = sRGB-like); <= 0 = off
    float g_DistortStrength; // 0 = no distortion this frame
    float g_Aspect;          // width / height (the offset is isotropic on screen)
    float2 _cpad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 ACES(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(PSInput i) : SV_TARGET
{
    float2 uv = i.uv;
    if (g_DistortStrength > 0.0)
    {
        float2 d = distortTex.Sample(linearClamp, i.uv).xy * g_DistortStrength;
        d.y *= g_Aspect;   // the same pixel distance in both directions
        uv = saturate(i.uv + d);
    }
    float3 scene = sceneTex.Sample(linearClamp, uv).rgb;
    float3 bloom = bloomTex.Sample(linearClamp, uv).rgb;

    float3 c = (scene + bloom * g_BloomIntensity) * g_Exposure;

    if (g_Tonemap != 0u)
        c = ACES(c);
    if (g_Gamma > 0.0)
        c = pow(max(c, 0.0), 1.0 / g_Gamma);

    return float4(c, 1.0);
}