// ============================================================
// WaterPS.hlsl
// The pond water surface (World/WaterSurface, 2026-10-27). Toon water
// that only moves when something moves it (the user: "water that keeps
// moving with nothing acting on it looks wrong"):
//   wind     ripples appear only where the local air speed (the world
//            wind with the same gust waves as the grass, plus the wind
//            sources: twisters, the vortex, gusts, sand tornadoes)
//            passes g_CalmWind, and drift downwind (two-phase flow so
//            the pattern never stretches). Calm air = a still mirror
//   ripples  height field from WaterRippleCS (steps, landings, rain,
//            blasts). Its slope tilts the normal, its crests turn white
//   still    body color in three toon steps from the shore to the middle,
//            a thin foam line on the shore (wider when a wave arrives),
//            the sun's hard glint, the sky in the toon fresnel
// Premultiplied alpha (ONE, INV_SRC_ALPHA), depth read only.
// The light cbuffer sits at b1 because WaterCB owns b0.
// ============================================================
#define MODEL_LIGHT_CB_REG b1
#include "../Common/Lighting.hlsli"
#include "../Common/WindSources.hlsli"
#include "WaterCommon.hlsli"

Texture2D<float2> g_Ripple : register(t0);                    // x = height (m), y = last step's
StructuredBuffer<WindSource> g_WindSources : register(t1);
SamplerState g_Linear : register(s0);

// ---- gradient noise with analytic derivatives (value, d/dx, d/dy) ----
float2 WaterHash(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

float3 NoiseD(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    float2 ga = WaterHash(i + float2(0.0, 0.0));
    float2 gb = WaterHash(i + float2(1.0, 0.0));
    float2 gc = WaterHash(i + float2(0.0, 1.0));
    float2 gd = WaterHash(i + float2(1.0, 1.0));
    float va = dot(ga, f - float2(0.0, 0.0));
    float vb = dot(gb, f - float2(1.0, 0.0));
    float vc = dot(gc, f - float2(0.0, 1.0));
    float vd = dot(gd, f - float2(1.0, 1.0));
    float v = va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * (va - vb - vc + vd);
    float2 d = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.x * u.y * (ga - gb - gc + gd)
             + du * (u.yx * (va - vb - vc + vd) + float2(vb, vc) - va);
    return float3(v, d);
}

// ---- the air over the water (same gust waves as the grass) ----
float GustWave(float2 xz, float2 dir, float perMeter, float speed)
{
    float g = sin(dot(xz, dir) * perMeter * 6.2831853 - g_WindTime * speed) * 0.5 + 0.5;
    return g * g;
}

float2 AirAt(float2 xz)
{
    float g1 = GustWave(xz, g_WindDir, g_WindScale, g_WindWaveSpeed);
    float g2 = GustWave(xz, g_Wave2.xy, g_Wave2.z, g_Wave2.w);
    float gust = g1 + g_Wave2Weight * g2 * (1.0 - g1);
    float2 v = g_WindDir * (g_WindSpeed + g_WindGust * gust);
    if (g_HasSources > 0.5)
    {
        float lift;
        v += WindSourceAir(g_WindSources, xz, lift).xz;
    }
    return v;
}

// slope of the wind ripples: noise in a frame stretched across the world
// wind (crests lie across the wind), carried downwind by the local air.
// Two phases half a period apart, each faded out where it jumps back
float2 WindSlope(float2 xz, float2 air)
{
    const float period = 1.6;
    float t = g_Time / period;
    float p0 = frac(t);
    float p1 = frac(t + 0.5);
    float w0 = 1.0 - abs(2.0 * p0 - 1.0);
    float2 fwd = g_WindDir;
    float2 side = float2(-fwd.y, fwd.x);
    float2 flow = air * (g_WaveFlow * period);

    float2 slope = 0;
    [unroll]
    for (int k = 0; k < 2; ++k)
    {
        float ph = (k == 0) ? p0 : p1;
        float w = (k == 0) ? w0 : 1.0 - w0;
        float2 p = xz - flow * ph + (k == 0 ? float2(0.0, 0.0) : float2(17.3, 5.9));
        // stretched frame: along the wind x1, across x0.45 (long crests across the wind)
        float2 q = float2(dot(p, fwd), dot(p, side) * 0.45) * g_WaveScale;
        float3 n1 = NoiseD(q);
        float3 n2 = NoiseD(q * 2.17 + float2(3.1, 7.7));
        float2 dq = n1.yz + 0.5 * 2.17 * n2.yz;                  // d/dq
        float2 dl = float2(dq.x, dq.y * 0.45) * g_WaveScale;     // d/d(local)
        slope += w * (fwd * dl.x + side * dl.y);                 // back to world xz
    }
    return slope;
}

float4 main(WaterOut i) : SV_TARGET
{
    float3 P = i.world;
    float2 xz = P.xz;

    // ---- ripples (wave field) ----
    float2 uv = (xz - g_MapOrigin) / g_MapSize;
    float du = 1.0 / WATER_RIPPLE_SIZE;
    float texelM = g_MapSize.x / WATER_RIPPLE_SIZE;
    float h = g_Ripple.SampleLevel(g_Linear, uv, 0).x;
    float hx0 = g_Ripple.SampleLevel(g_Linear, uv - float2(du, 0.0), 0).x;
    float hx1 = g_Ripple.SampleLevel(g_Linear, uv + float2(du, 0.0), 0).x;
    float hz0 = g_Ripple.SampleLevel(g_Linear, uv - float2(0.0, du), 0).x;
    float hz1 = g_Ripple.SampleLevel(g_Linear, uv + float2(0.0, du), 0).x;
    float2 rippleSlope = float2(hx1 - hx0, hz1 - hz0) / (2.0 * texelM);

    // ---- wind ripples: only where the air is fast enough ----
    float2 air = AirAt(xz);
    float amp = saturate((length(air) - g_CalmWind) / max(g_FullWind - g_CalmWind, 0.01));
    float2 windSlope = 0;
    if (amp > 0.001)
        windSlope = WindSlope(xz, air) * (amp * g_WaveNormal);

    float2 slope = rippleSlope * g_RippleNormal + windSlope;
    // at most ~25 degrees of tilt: a landing or a blast tilted the surface 45-60 degrees and the whole
    // patch mirrored the pale sky (a solid white disc under the feet)
    float slopeLen = length(slope);
    slope *= min(1.0, 0.45 / max(slopeLen, 1e-5));
    float3 N = normalize(float3(-slope.x, 1.0, -slope.y));
    float3 V = normalize(cameraPosition - P);
    float3 L = normalize(-dirLight.direction);

    // ---- body: shallow -> deep in three toon steps ----
    // fixed noise on the distance: the steps follow the shore loosely instead of making a rectangle
    float k = saturate(i.shore / max(g_DeepDist, 0.01) + 0.22 * NoiseD(xz * 0.32 + float2(4.7, 1.3)).x);
    float band = 0.5 * smoothstep(0.24, 0.30, k) + 0.5 * smoothstep(0.62, 0.68, k);
    float3 body = lerp(g_Shallow.rgb, g_Deep.rgb, band);
    float alpha = lerp(g_Shallow.a, g_Deep.a, band);
    body = lerp(body, g_Turbid.rgb, g_Turbid.a);
    alpha = lerp(alpha, 0.96, g_Turbid.a);

    // ---- light (the water lies flat for the sun and its shadow; the ripples only tilt the reflections) ----
    float3 up = float3(0.0, 1.0, 0.0);
    float NdotL = max(dot(up, L), 0.0);
    uint cascade;
    float shadow = SunShadow(P, up, NdotL, cascade);
    float lit = ToonBand(NdotL * shadow);
    float3 sun = dirLight.color * (dirLight.intensity * toonShadowTint.w * lit);
    float3 ambient = ambientColor * lerp(toonShadowTint.rgb, float3(1.0, 1.0, 1.0), lit);
    float3 light = ambient + sun;
    float3 c = body * light;

    // facets of the waves facing the sun a little lighter, the others darker (two steps)
    float2 Lxz = normalize(L.xz + float2(1e-4, 0.0));
    float facing = dot(-slope, Lxz);
    c *= 1.0 + 0.16 * smoothstep(0.05, 0.09, facing) - 0.08 * smoothstep(0.05, 0.09, -facing);
    // where the wind roughens the water it mirrors less of the sky: a touch darker
    c *= 1.0 - g_WindDarken * amp;

    // ---- the sky in the fresnel (two steps, a little even straight down) ----
    float NdotV = saturate(dot(N, V));
    float fres = pow(1.0 - NdotV, 4.0);
    float fb = 0.25 + 0.30 * smoothstep(0.03, 0.05, fres) + 0.45 * smoothstep(0.18, 0.22, fres);
    float3 R = reflect(-V, N);
    float3 sky = lerp(g_SkyHorizon.rgb, g_SkyZenith.rgb, sqrt(saturate(R.y)));
    float refl = fb * g_SkyReflect * (1.0 - 0.5 * amp) * (1.0 - g_Turbid.a * 0.7);
    c = lerp(c, sky * (0.55 + 0.45 * lit), refl);
    alpha = lerp(alpha, 1.0, refl * 0.5);

    // ---- white: wave crests and the shore line ----
    // wave crests as thin toon lines along height contours (about 3 px wide whatever the slope), not
    // filled areas: filling everything above the threshold turned a step or a landing into a solid white disc.
    // Two levels, so a big wave (landing, blast) shows two rings
    float hw = max(fwidth(h), 1e-6);
    float crest = saturate(1.0 - abs(h - g_RippleCrest) / (hw * 1.5));
    crest = max(crest, 0.7 * saturate(1.0 - abs(h - g_RippleCrest * 3.0) / (hw * 1.5)));
    float arrive = saturate(abs(h) / max(g_RippleCrest, 1e-4));
    float fw = g_Foam.a * (1.0 + 1.0 * arrive);
    float wobble = NoiseD(xz * 2.3).x;   // fixed: the line has an uneven edge, it does not crawl
    float foam = 1.0 - smoothstep(fw * 0.7, fw, i.shore + wobble * fw * 0.3);
    float white = saturate(max(crest * 0.85, foam));
    c = lerp(c, g_Foam.rgb * light, white);
    alpha = lerp(alpha, 1.0, white);

    // ---- the sun's glint: a hard window, only where the surface tilts toward it ----
    float3 H = normalize(V + L);
    // about 1.5 degrees: on still water just the sun's own small spot, on ripples a scatter of sparkles
    float glint = smoothstep(0.99955, 0.99975, saturate(dot(N, H))) * lit * (1.0 - white);
    float3 add = dirLight.color * (dirLight.intensity * g_SpecStrength * glint);

    float d = distance(P, cameraPosition);
    float fogF = smoothstep(fogStart, max(fogEnd, fogStart + 1e-3), d) * fogMax;
    c = lerp(c, fogColor, fogF);
    add *= 1.0 - fogF;
    return float4(c * alpha + add, alpha);
}
