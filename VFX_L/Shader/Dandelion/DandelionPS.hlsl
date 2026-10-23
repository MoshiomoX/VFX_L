// ============================================================
// DandelionPS.hlsl
// Cuts the dandelion parts out of their cards and lights them like
// every model (Lighting.hlsli: toon bands, sun shadow, point lights,
// fog). Opaque, writes depth (drawn with the grass, after the outline).
//   stem : a strip tapering toward the head
//   head : a white fluffy ball (ragged rim of pappus tufts, a small
//          brown seed cluster in the middle), lit as a sphere
//   seed : a tiny star of pappus rays around a brown dot
// The light cbuffer sits at b1 because DandelionCB owns b0.
// ============================================================
#define MODEL_LIGHT_CB_REG b1
#include "../Common/Lighting.hlsli"
#include "DandelionCommon.hlsli"

struct DandelionOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint part : TEXCOORD3;
    nointerpolation uint seed : TEXCOORD4;
};

float4 main(DandelionOut i) : SV_TARGET
{
    float3 albedo;
    float3 n = normalize(i.normal);
    float r = length(i.uv);
    float ang = atan2(i.uv.y, i.uv.x);
    float var = (float) (i.seed & 0xFFu) / 255.0;

    if (i.part == 0u)
    {
        float t = i.uv.y * 0.5 + 0.5;
        float w = 1.0 - 0.45 * t;
        if (abs(i.uv.x) > w)
            discard;
        albedo = g_StemColor * (0.85 + 0.3 * var) * lerp(0.8, 1.05, t);
    }
    else if (i.part == 1u)
    {
        // ragged rim: tufts around the edge (two frequencies so heads differ)
        float tufts = 0.5 + 0.5 * cos(ang * 13.0 + var * 6.28) * cos(ang * 5.0 + var * 17.0);
        float rim = 0.78 + 0.2 * tufts;
        if (r > rim)
            discard;
        // sphere normal from the disc: the camera-facing card stands in for a ball
        float3 toCam = n;
        float3 sphereN = normalize(g_CamRight * i.uv.x + g_CamUp * i.uv.y + toCam * sqrt(saturate(1.0 - r * r)));
        n = sphereN;
        albedo = g_PuffColor * (0.9 + 0.1 * var);
        // fine streaks of the pappus radiating out
        albedo *= 0.9 + 0.1 * (0.5 + 0.5 * cos(ang * 41.0 + var * 3.0));
        if (r < 0.16)
            albedo = float3(0.42, 0.33, 0.16);   // the seed cluster in the middle
    }
    else
    {
        // a pappus star: 6 thin rays + the seed dot
        float ray = abs(sin(ang * 3.0 + var * 6.28)) * r;
        bool keep = (r < 0.28) || (ray < 0.13 && r < 1.0);
        if (!keep)
            discard;
        albedo = (r < 0.2) ? float3(0.42, 0.33, 0.16) : g_PuffColor;
    }

    float3 lit = ShadeLambert(n, albedo, i.world);
    return float4(lit, 1.0);
}
