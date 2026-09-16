// ============================================================
// CompositePS.hlsl
// scene + bloom -> exposure -> (tonemap) -> gamma -> backbuffer.
// The only place in the pipeline that leaves linear HDR.
// ============================================================
Texture2D sceneTex : register(t0);
Texture2D bloomTex : register(t1);
SamplerState linearClamp : register(s0);

cbuffer CompositeCB : register(b0)
{
    float g_BloomIntensity;
    float g_Exposure;
    uint g_Tonemap; // 0 = off, 1 = ACES
    uint g_Gamma; // 0 = off, 1 = pow(1/2.2)
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
    float3 scene = sceneTex.Sample(linearClamp, i.uv).rgb;
    float3 bloom = bloomTex.Sample(linearClamp, i.uv).rgb;

    float3 c = (scene + bloom * g_BloomIntensity) * g_Exposure;

    if (g_Tonemap != 0u)
        c = ACES(c);
    if (g_Gamma != 0u)
        c = pow(max(c, 0.0), 1.0 / 2.2);

    return float4(c, 1.0);
}