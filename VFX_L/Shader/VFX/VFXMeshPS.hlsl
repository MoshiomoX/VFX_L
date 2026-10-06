// ============================================================
// VFXMeshPS.hlsl
// Unlit VFX material: main * tint * intensity (linear HDR out).
//   noise : scrolls, distorts main UV, drives dissolve
//   mask  : multiplies alpha
//   shade : (2026-10-06) fake sphere look - two-band shading from a
//           fixed light direction, a rim light toward the camera and a
//           small highlight. Reads no scene light (pure look).
// Sits on the same VS_OUTPUT as lit models but reads no light.
// ============================================================
#include "../Common/ModelCommon.hlsli"

Texture2D mainTex : register(t0);
Texture2D noiseTex : register(t1);
Texture2D maskTex : register(t2);
SamplerState linearWrap : register(s0);

cbuffer VFXMeshCB : register(b1)
{
    float4 g_Tint;
    float2 g_MainTiling;
    float2 g_MainScroll;
    float2 g_NoiseTiling;
    float2 g_NoiseScroll;
    float g_Distortion;
    float g_Intensity;
    float g_DissolveThreshold; // < 0 : off
    float g_DissolveEdge;
    float4 g_DissolveEdgeColor;
    uint g_HasNoise;
    uint g_HasMask;
    float g_Shade;             // 0 = flat, 1 = full sphere shading
    float _pad;
    float3 g_CamPos;
    float _pad2;
};

float4 main(PS_INPUT i) : SV_TARGET
{
    float2 uv = i.UV;

    // ---- noise (single channel) ----
    float n = 0.5;
    if (g_HasNoise != 0u)
        n = noiseTex.Sample(linearWrap, uv * g_NoiseTiling + g_NoiseScroll).r;

    // ---- main, distorted by noise ----
    float2 mainUV = uv * g_MainTiling + g_MainScroll + (n - 0.5) * g_Distortion;
    float4 c = mainTex.Sample(linearWrap, mainUV) * g_Tint * i.Color;

    // ---- mask ----
    if (g_HasMask != 0u)
        c.a *= maskTex.Sample(linearWrap, uv).r;

    // ---- dissolve ----
    if (g_DissolveThreshold >= 0.0 && g_HasNoise != 0u)
    {
        float d = n - g_DissolveThreshold;
        clip(d);
        float edge = 1.0 - saturate(d / max(g_DissolveEdge, 1e-4));
        c.rgb = lerp(c.rgb, g_DissolveEdgeColor.rgb, edge * g_DissolveEdgeColor.a);
    }

    // ---- sphere look: fixed key light (upper left, toward the camera side), rim, highlight ----
    if (g_Shade > 0.0)
    {
        float3 N = normalize(i.Normal);
        float3 V = normalize(g_CamPos - i.WorldPos);
        float3 L = normalize(float3(-0.45, 0.75, -0.35) + V * 0.6);   // leans toward the viewer: the lit side faces the camera
        float ndl = saturate(dot(N, L));
        float band = smoothstep(0.12, 0.3, ndl);                       // two bands like the toon models
        float shadeMul = lerp(0.42, 1.0, band);
        float rim = pow(1.0 - saturate(dot(N, V)), 3.0);
        float3 H = normalize(L + V);
        float spec = pow(saturate(dot(N, H)), 60.0) * 0.35;
        float3 shaded = c.rgb * shadeMul + c.rgb * rim * 0.45 + spec * c.a;
        c.rgb = lerp(c.rgb, shaded, g_Shade);
    }

    c.rgb *= g_Intensity;
    return c;
}