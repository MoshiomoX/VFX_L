// ============================================================
// DebrisPS.hlsl
// Cuts the piece's shape out of the card (leaf: pointed ellipse with a
// midrib; clipping / twig: tapered strip) and lights it like every
// model (Lighting.hlsli: toon bands, sun shadow, point lights, fog).
// Opaque, writes depth (drawn with the grass, after the outline pass).
// The light cbuffer sits at b1 because DebrisCB owns b0.
// ============================================================
#define MODEL_LIGHT_CB_REG b1
#include "../Common/Lighting.hlsli"
#include "DebrisCommon.hlsli"

struct DebrisOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint seed : TEXCOORD3;
};

float4 main(DebrisOut i) : SV_TARGET
{
    uint kind = i.seed & DEBRIS_KIND_MASK;
    float x = i.uv.x, y = abs(i.uv.y);
    float3 albedo;
    if (kind == 0u)
    {
        // leaf: ellipse pulled to a point at +x, a darker midrib
        float w = (1.0 - x * x) * (1.0 - 0.3 * x) * 1.05;
        if (y > w)
            discard;
        albedo = g_ColorLeaf;
        if (y < 0.07)
            albedo *= 0.78;
        // edge darkening
        albedo *= 1.0 - 0.25 * saturate((y - w + 0.25) / 0.25);
    }
    else
    {
        // clipping / twig: a strip tapering toward +x
        float w = 1.0 - 0.45 * (x + 1.0) * 0.5;
        if (y > w)
            discard;
        albedo = (kind == 1u) ? g_ColorClip : g_ColorTwig;
    }
    // per-piece variation: brightness and a touch toward brown
    float v = (float) ((i.seed >> 8) & 0xFFu) / 255.0;
    albedo *= 0.8 + 0.4 * v;
    albedo = lerp(albedo, albedo.rrr * float3(1.0, 0.75, 0.45), 0.35 * (float) ((i.seed >> 4) & 0x3u) / 3.0);

    float3 lit = ShadeLambert(normalize(i.normal), albedo, i.world);
    return float4(lit, 1.0);
}
