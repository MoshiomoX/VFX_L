// ============================================================
// SwarmSpritePS.hlsl
// Same output as VFXSpritePS, the sheet is a slice of the sprite
// texture array (flags bits 8-15).
// ============================================================
#include "../Common/SpriteQuad.hlsli"

Texture2DArray g_Sheets : register(t0);
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
    float slice = (float) ((i.flags >> 8) & 0xFFu);
    float4 t = g_Sheets.Sample(g_Samp, float3(i.uv, slice));
    float a = t.a * i.color.a;
    clip(a - 0.004);
    float3 rgb = t.rgb * i.color.rgb * a;
    return float4(rgb, (i.flags & SPRITE_FLAG_ADDITIVE) ? 0.0 : a);
}
