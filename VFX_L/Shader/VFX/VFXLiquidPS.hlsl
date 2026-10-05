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
    // the grid is a square; nothing is drawn outside the circle it was sized for
    float reachOut = R * L.fill + max(L.haloWidth, 0.0) + 0.05;
    if (dot(i.local, i.local) > reachOut * reachOut)
        discard;
    float d = LiquidSDF(L, i.local, seed, R, age, left, g_LTime, g);
    float px = max(fwidth(d), 1e-4);
    float cover = saturate(0.5 - d / px) * i.mask;
    float halo = (d > 0.0) ? (1.0 - smoothstep(0.0, max(L.haloWidth, 1e-3), d)) * L.haloGain * i.mask : 0.0;
    if (cover <= 0.002 && halo <= 0.002)
        discard;

    float inside = -d;
    float deep = smoothstep(0.0, max(L.depthWidth, 1e-3), inside);
    float rim = 1.0 - smoothstep(0.0, max(L.rimWidth, 1e-3), inside);
    // toon (2026-10-04, follows the scene's toonParams): crisp zones instead of smooth ramps
    bool toon = toonParams.x > 0.5;
    if (toon)
    {
        float dw = max(L.depthWidth, 1e-3);
        deep = smoothstep(dw * 0.5 - px, dw * 0.5 + px, inside);
        float rw0 = max(L.rimWidth, 1e-3);
        rim = 1.0 - smoothstep(rw0 * 0.6 - px, rw0 * 0.6 + px, inside);
    }

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
    float lit = toon ? ToonBand(NdotL * shadow) : NdotL * shadow;
    float3 sun = dirLight.color * dirLight.intensity * lit * (toon ? toonShadowTint.w : 1.0);

    // toon: the swirl is two-level patches
    float swirlN = toon ? smoothstep(0.5 - fwidth(n0), 0.5 + fwidth(n0), n0) : n0;
    float3 albedo = lerp(L.edgeColor.rgb, L.deepColor.rgb, deep) * (1.0 + L.swirl * (swirlN - 0.5) * 2.0);
    const float3 F0 = float3(0.02, 0.02, 0.02);   // water-like
    float3 spec;
    float fres;
    if (toon)
    {
        // a small hard highlight and a crisp band of sky at grazing angles
        float3 H = normalize(V + Ld);
        spec = dirLight.color * (dirLight.intensity * L.specGain * 0.6 * lit)
             * smoothstep(0.990, 0.995, saturate(dot(N, H)));
        fres = smoothstep(0.74, 0.76, 1.0 - saturate(dot(N, V))) * 0.5;
    }
    else
    {
        spec = SpecularGGX(N, V, Ld, max(L.roughness, 0.04), F0) * sun * L.specGain;
        fres = pow(1.0 - saturate(dot(N, V)), 5.0);
    }
    float3 sky = L.skyColor.rgb * (fres * L.skyColor.a);
    // pointGain 0 skips the light loop: every pool brings its own light, so with N stacked
    // pools each pixel of each pool walked N lights (2026-10-05, poison pools)
    float3 pd = float3(0.0, 0.0, 0.0);
    float3 ps = float3(0.0, 0.0, 0.0);
    if (L.pointGain > 0.0)
        PointLightShade(i.world, N, V, max(L.roughness, 0.04), F0, pd, ps);
    // a liquid pool usually carries its own light just above it (Poison: 0.5m); at full
    // strength its highlight on the liquid went far past the bloom threshold
    pd *= L.pointGain;
    ps *= L.pointGain;
    // a liquid shows little diffuse; most of what you see is what it reflects and emits
    float3 ambient = toon ? AmbientAt(N) * lerp(toonShadowTint.rgb, float3(1, 1, 1), lit) : AmbientAt(N);
    float3 diffuse = albedo * (ambient + sun + pd) * 0.6;
    float3 glow = L.glowColor.rgb * (1.0 + L.glowColor.a * rim);
    float bub = LiquidBubbles(L, i.local, seed, deep, g_LTime) * L.bubbleGain;

    float3 c = diffuse + spec + ps + sky + glow + bub * (L.edgeColor.rgb + 0.3);
    // toon: a dark ink line just inside the edge (at least 1.5 px wide)
    if (toon)
    {
        float lineW = max(0.035, px * 1.5);
        c *= lerp(0.25, 1.0, smoothstep(lineW - px, lineW + px, inside));
    }
    c = ApplyFog(c, i.world);
    float alpha = cover * lerp(L.edgeColor.a, L.deepColor.a, deep);

    // halo: emissive only, added on top of whatever is below (alpha 0). Added light is
    // dimmed by the fog instead of blended toward its colour (that would brighten the ground)
    float fogF = smoothstep(fogStart, max(fogEnd, fogStart + 1e-3), distance(i.world, cameraPosition)) * fogMax;
    float3 haloRgb = L.glowColor.rgb * (1.0 + L.glowColor.a) * halo * (1.0 - fogF) * (1.0 - cover);
    return float4(c * alpha + haloRgb, alpha);
}
