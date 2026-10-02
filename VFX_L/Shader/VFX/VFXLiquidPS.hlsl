// ============================================================
// VFXLiquidPS.hlsl
// Liquid entry (2026-10-02): shades one pixel of a puddle. Used by both
// the CPU path (VFXLiquidRenderer + VFXLiquidVS) and the GPU path
// (SwarmSystem + SwarmLiquidVS); see LiquidCommon.hlsli.
//
//   coverage : the puddle SDF d (m), antialiased over fwidth(d)
//   depth    : 0 at the edge -> 1 at depthWidth inside (colour, opacity)
//   surface  : flat in the middle, curving down over depthWidth at the edge,
//              its normal from the SDF gradient + a slow noise ripple
//   light    : sun (diffuse + GGX highlight, sun shadow), sky fresnel,
//              point lights, emissive glow (stronger at the rim), bubbles
//   halo     : a soft additive glow on the ground just outside the edge
// Output is premultiplied (ONE / INV_SRC_ALPHA); the halo adds with alpha 0.
//
// b0 = LightBuffer (sun, camera, fog, shadows), b1 = LiquidFrameCB,
// t0 = the LiquidDef table
// ============================================================
#include "../Common/Lighting.hlsli"
#include "../Common/LiquidCommon.hlsli"

cbuffer LiquidFrameCB : register(b1)
{
    float g_LTime;   // animation clock, seconds
    float3 _liquidFramePad;
};

StructuredBuffer<LiquidDef> liquidDefs : register(t0);

float4 main(LiquidVSOut i) : SV_TARGET
{
    LiquidDef L = liquidDefs[i.ids.x];
    float R = i.info.x;
    float age = i.info.y;
    float left = i.info.z;
    uint seed = i.ids.y;

    // ---- shape ----
    float2 g;
    float d = LiquidSDF(L, i.local, seed, R, age, left, g_LTime, g);
    float px = max(fwidth(d), 1e-4);
    float cover = saturate(0.5 - d / px) * i.mask;
    float halo = (d > 0.0) ? (1.0 - smoothstep(0.0, max(L.haloWidth, 1e-3), d)) * L.haloGain * i.mask : 0.0;
    if (cover <= 0.002 && halo <= 0.002)
        discard;

    float inside = -d;
    float deep = smoothstep(0.0, max(L.depthWidth, 1e-3), inside);
    float rim = 1.0 - smoothstep(0.0, max(L.rimWidth, 1e-3), inside);

    // ---- surface normal ----
    // height h(d) = bump * S(-d / depthWidth), S = smoothstep  ->  grad h = -slope * grad d
    // N = normalize(-dh/dx, 1, -dh/dz) = normalize(slope * grad d, 1)
    float2 fwd = i.frame.xy;
    float2 lft = i.frame.zw;
    float x = saturate(inside / max(L.depthWidth, 1e-3));
    float slope = L.bump * 6.0 * x * (1.0 - x) / max(L.depthWidth, 1e-3);
    float2 gw = g.x * fwd + g.y * lft;   // local -> world xz
    // slow swirl: brightness and a small ripple (finite differences of the noise)
    float2 sp = i.local * L.swirlScale + float2(g_LTime * 0.15, -g_LTime * 0.11);
    float n0 = LiquidNoise(sp);
    float nx = LiquidNoise(sp + float2(0.25, 0.0));
    float ny = LiquidNoise(sp + float2(0.0, 0.25));
    float2 ripple = (float2(n0 - nx, n0 - ny) * (2.0 * L.swirl * deep));
    float2 rw = ripple.x * fwd + ripple.y * lft;
    float3 N = normalize(float3(slope * gw.x + rw.x, 1.0, slope * gw.y + rw.y));

    // ---- light ----
    float3 V = normalize(cameraPosition - i.world);
    float3 Ld = normalize(-dirLight.direction);
    float NdotL = saturate(dot(N, Ld));
    uint cascade;
    float shadow = SunShadow(i.world, float3(0.0, 1.0, 0.0), NdotL, cascade);
    float3 sun = dirLight.color * dirLight.intensity * NdotL * shadow;

    float3 albedo = lerp(L.edgeColor.rgb, L.deepColor.rgb, deep) * (1.0 + L.swirl * (n0 - 0.5) * 2.0);
    const float3 F0 = float3(0.02, 0.02, 0.02);   // water-like
    float3 spec = SpecularGGX(N, V, Ld, max(L.roughness, 0.04), F0) * sun * L.specGain;
    float fres = pow(1.0 - saturate(dot(N, V)), 5.0);
    float3 sky = L.skyColor.rgb * (fres * L.skyColor.a);
    float3 pd, ps;
    PointLightShade(i.world, N, V, max(L.roughness, 0.04), F0, pd, ps);
    // a liquid pool usually carries its own light just above it (Poison: 0.5m); at full
    // strength its highlight on the liquid went far past the bloom threshold
    pd *= L.pointGain;
    ps *= L.pointGain;
    // a liquid shows little diffuse; most of what you see is what it reflects and emits
    float3 diffuse = albedo * (AmbientAt(N) + sun + pd) * 0.6;
    float3 glow = L.glowColor.rgb * (1.0 + L.glowColor.a * rim);
    float bub = LiquidBubbles(L, i.local, seed, deep, g_LTime) * L.bubbleGain;

    float3 c = diffuse + spec + ps + sky + glow + bub * (L.edgeColor.rgb + 0.3);
    c = ApplyFog(c, i.world);
    float alpha = cover * lerp(L.edgeColor.a, L.deepColor.a, deep);

    // halo: emissive only, added on top of whatever is below (alpha 0). Added light is
    // dimmed by the fog instead of blended toward its colour (that would brighten the ground)
    float fogF = smoothstep(fogStart, max(fogEnd, fogStart + 1e-3), distance(i.world, cameraPosition)) * fogMax;
    float3 haloRgb = L.glowColor.rgb * (1.0 + L.glowColor.a) * halo * (1.0 - fogF) * (1.0 - cover);
    return float4(c * alpha + haloRgb, alpha);
}
