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
//   ice      a pale sheet with cracks where g_Ice is frozen (WaterIceCS),
//            melting from scattered spots as it runs out (2026-11-01)
// Premultiplied alpha (ONE, INV_SRC_ALPHA), depth read only.
// The light cbuffer sits at b1 because WaterCB owns b0.
// ============================================================
#define MODEL_LIGHT_CB_REG b1
#include "../Common/Lighting.hlsli"
#include "../Common/WindSources.hlsli"
#include "WaterCommon.hlsli"

Texture2D<float4> g_Ripple : register(t0);   // x = height (m), y = last step's; zw = the same for the big waves only
StructuredBuffer<WindSource> g_WindSources : register(t1);
Texture2D<float> g_Ice : register(t2);       // 1 = just frozen .. 0 = open water (WaterIceCS)
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

// ---- cracks in the ice: lines along the cell borders of a Voronoi pattern (1 on a crack) ----
float IceCrack(float2 p)
{
    float2 cell = floor(p);
    float2 f = p - cell;
    float d1 = 8.0, d2 = 8.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            float2 g = float2(x, y);
            float2 o = 0.5 + 0.5 * WaterHash(cell + g);
            float d = length(g + o - f);
            if (d < d1) { d2 = d1; d1 = d; }
            else if (d < d2) d2 = d;
        }
    return 1.0 - smoothstep(0.015, 0.05, d2 - d1);
}

// ---- the ripple field, smoothed: cubic B-spline in 4 bilinear taps ----
// plain bilinear showed the 0.2 m texels as corners (a footstep ring was a pentagon)
float4 SampleRipple(float2 uv)
{
    float2 st = uv * WATER_RIPPLE_SIZE - 0.5;
    float2 i = floor(st);
    float2 f = st - i;
    float2 f2 = f * f;
    float2 f3 = f2 * f;
    float2 w0 = (-f3 + 3.0 * f2 - 3.0 * f + 1.0) / 6.0;
    float2 w1 = (3.0 * f3 - 6.0 * f2 + 4.0) / 6.0;
    float2 w2 = (-3.0 * f3 + 3.0 * f2 + 3.0 * f + 1.0) / 6.0;
    float2 w3 = f3 / 6.0;
    float2 g0 = w0 + w1;
    float2 g1 = w2 + w3;
    float2 t0 = (i - 0.5 + w1 / g0) / WATER_RIPPLE_SIZE;
    float2 t1 = (i + 1.5 + w3 / g1) / WATER_RIPPLE_SIZE;
    return g0.y * (g0.x * g_Ripple.SampleLevel(g_Linear, t0, 0) + g1.x * g_Ripple.SampleLevel(g_Linear, float2(t1.x, t0.y), 0))
         + g1.y * (g0.x * g_Ripple.SampleLevel(g_Linear, float2(t0.x, t1.y), 0) + g1.x * g_Ripple.SampleLevel(g_Linear, t1, 0));
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
    float4 hs = SampleRipple(uv);
    float h = hs.x;
    float bigH = hs.z;              // the big waves' share (blasts, the boss): only they draw white lines
    float rise = hs.z - hs.w;       // their rise this step (m): + = going up, - = going down
    float4 sx0 = SampleRipple(uv - float2(du, 0.0));
    float4 sx1 = SampleRipple(uv + float2(du, 0.0));
    float4 sz0 = SampleRipple(uv - float2(0.0, du));
    float4 sz1 = SampleRipple(uv + float2(0.0, du));
    float2 rippleSlope = float2(sx1.x - sx0.x, sz1.x - sz0.x) / (2.0 * texelM);

    // is the big waves' top here a long crest (a ring running out) or a lone mound? Curvature of zw:
    // a crest bends down only across itself (one strong negative, one near zero), a mound bends down
    // every way. The middle of a blast springs back up as a mound and bounced waves pile up into mounds:
    // they drew small white circles (the user: "small circles after the blast")
    float bxx = sx1.z + sx0.z - 2.0 * bigH;
    float bzz = sz1.z + sz0.z - 2.0 * bigH;
    float bxz = 0.25 * (SampleRipple(uv + float2(du, du)).z - SampleRipple(uv + float2(du, -du)).z
                      - SampleRipple(uv + float2(-du, du)).z + SampleRipple(uv + float2(-du, -du)).z);
    float bMid = 0.5 * (bxx + bzz);
    float bRad = sqrt(0.25 * (bxx - bzz) * (bxx - bzz) + bxz * bxz);
    float across = max(-(bMid - bRad), 0.0);   // the stronger downward bend
    float along = abs(bMid + bRad);            // the other one
    float ridge = (across - along) / (across + along + 1e-9);

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
    // the ripples tilt it only a little: at full tilt the slope of a landing or a step that leans away from
    // the camera passed the top step and mirrored the pale sky as a white patch around the feet
    float2 fslope = rippleSlope * (g_RippleNormal * 0.35) + windSlope;
    fslope *= min(1.0, 0.45 / max(length(fslope), 1e-5));
    float3 NF = normalize(float3(-fslope.x, 1.0, -fslope.y));
    float NdotV = saturate(dot(NF, V));
    float fres = pow(1.0 - NdotV, 4.0);
    float fb = 0.25 + 0.30 * smoothstep(0.03, 0.05, fres) + 0.45 * smoothstep(0.18, 0.22, fres);
    float3 R = reflect(-V, N);
    float3 sky = lerp(g_SkyHorizon.rgb, g_SkyZenith.rgb, sqrt(saturate(R.y)));
    float refl = fb * g_SkyReflect * (1.0 - 0.5 * amp) * (1.0 - g_Turbid.a * 0.7);
    c = lerp(c, sky * (0.55 + 0.45 * lit), refl);
    alpha = lerp(alpha, 1.0, refl * 0.5);

    // ---- white: wave crests and the shore line ----
    // one thin toon line on the top of each crest: where the water stops rising and starts to fall
    // (this step's rise crosses zero) and stands above g_RippleCrest. About 2-3 px wide whatever the slope.
    // Filling everything above the threshold made a step or a landing a solid white disc; lines along a
    // height contour drew every crest twice, once on each side of the top (the user: the double rings look
    // wrong). A taller crest gets a slightly bolder line instead of a second ring.
    // Only the big waves (zw) draw it: the player's own steps and landings show as light and shade only
    // (the user: "there are still small circles" = the ring around the feet after a landing)
    float above = (bigH - g_RippleCrest) / g_RippleCrest;
    float rw = max(fwidth(rise), g_RippleCrest * 1e-3) * (1.1 + 0.6 * saturate(above * 0.5));
    float crest = saturate(1.0 - abs(rise) / rw) * smoothstep(-0.4, 0.2, above) * smoothstep(0.45, 0.75, ridge);
    float arrive = saturate(abs(h) / max(g_RippleCrest, 1e-4));
    float fw = g_Foam.a * (1.0 + 1.0 * arrive);
    float wobble = NoiseD(xz * 2.3).x;   // fixed: the line has an uneven edge, it does not crawl
    float foam = 1.0 - smoothstep(fw * 0.7, fw, i.shore + wobble * fw * 0.3);
    float white = saturate(max(crest * 0.85, foam));
    c = lerp(c, g_Foam.rgb * light, white);
    alpha = lerp(alpha, 1.0, white);

    // ---- ice: a pale sheet over the water, cracked; it melts from scattered spots near the end ----
    float iceV = g_Ice.SampleLevel(g_Linear, uv, 0);
    float meltAt = 0.04 + 0.30 * saturate(0.5 + NoiseD(xz * 0.9 + float2(7.1, 2.9)).x);
    float ice = smoothstep(meltAt - 0.02, meltAt + 0.02, iceV);
    if (ice > 0.001)
    {
        float crack = max(IceCrack(xz * 0.55), 0.6 * IceCrack(xz * 1.7 + float2(3.3, 8.1)));
        float3 sheet = lerp(float3(0.55, 0.78, 0.92), float3(0.86, 0.95, 1.0), crack);
        float3 iceCol = sheet * light + sky * 0.18;
        c = lerp(c, iceCol, ice);
        alpha = lerp(alpha, 0.95, ice);
    }

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
