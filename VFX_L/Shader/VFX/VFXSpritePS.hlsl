// ============================================================
// VFXSpritePS.hlsl
// Straight-alpha sheet x colour, written premultiplied for the
// ONE / INV_SRC_ALPHA blend. Additive sprites write alpha 0.
// ============================================================
#include "../Common/SpriteQuad.hlsli"

Texture2D g_Tex : register(t0);
SamplerState g_Samp : register(s0);

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    nointerpolation uint flags : TEXCOORD1;
};

float4 main(PSIn i) : SV_TARGET
{
    float4 t = g_Tex.Sample(g_Samp, i.uv);
    float a = t.a * i.color.a;
    clip(a - 0.004);
    float3 rgb = t.rgb * i.color.rgb * a;
    return float4(rgb, (i.flags & SPRITE_FLAG_ADDITIVE) ? 0.0 : a);
}
