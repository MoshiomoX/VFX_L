// ============================================================
// ParticleTrailPS.hlsl
// Ribbon trail pixel shader. texture * vertex color, optional soft
// edge across the width.
//   U runs along the ribbon (head -> tail), V across it.
//   g_Premultiply = 1 for the alpha-blend pass (ONE / INV_SRC_ALPHA),
//   0 for the additive pass (SRC_ALPHA / ONE).
// ============================================================
Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

cbuffer TrailDrawCB : register(b1)
{
    uint g_StyleSlot;
    uint g_Premultiply;
    float g_Time;
    float _padT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation float softEdge : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 c = g_Texture.Sample(g_Sampler, input.uv) * input.color;

    // 1 on the centre line, 0 on both edges
    float centre = 1.0 - abs(input.uv.y * 2.0 - 1.0);
    if (input.softEdge > 0.001)
        c.a *= saturate(centre / input.softEdge);

    clip(c.a - 0.003);

    if (g_Premultiply != 0u)
        c.rgb *= c.a;
    return c;
}
