// ============================================================
// BeamCommon.hlsli
// Shared by VFXBeamVS / VFXBeamPS: the beam item (C++ VFXBeamItem, 96 bytes),
// the layer table and the value noise that makes the beam "flow".
// ============================================================
#ifndef BEAM_COMMON_HLSLI
#define BEAM_COMMON_HLSLI

#define BEAM_SEGMENTS 32
#define BEAM_LAYERS 3

// layer 0 = glow (widest, faint), 1 = main colour, 2 = white core
struct BeamItem
{
    float3 start;
    float width; // full width of the main layer (m)
    float3 end;
    float coreRatio; // core width / main width
    float4 color; // main colour (HDR ok)
    float4 coreColor;
    float glowRatio; // glow width / main width
    float glowAlpha;
    float scroll; // noise phase along the beam (m)
    float noiseScale; // noise cells per metre
    float noiseStrength; // width wobble + brightness streaks (0..1)
    float tipFade; // metres over which the tip thins out
    float rootFade; // metres over which the root fades in
    float seed;
    float toon; // 1 = hard-edged bands + two-level streaks (2026-10-04)
    float3 toonPad;
};

// ---- value noise (hash based, no texture) ----
float BeamHash(float2 p)
{
    p = frac(p * float2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}

float BeamNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = BeamHash(i);
    float b = BeamHash(i + float2(1, 0));
    float c = BeamHash(i + float2(0, 1));
    float d = BeamHash(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// two octaves, 0..1
float BeamFbm(float2 p)
{
    return BeamNoise(p) * 0.65 + BeamNoise(p * 2.13 + 7.7) * 0.35;
}

// half width of a layer at distance d (m) along the beam of length len
float BeamHalfWidth(BeamItem b, uint layer, float d, float len)
{
    float ratio = (layer == 0) ? b.glowRatio : (layer == 2) ? b.coreRatio : 1.0;
    float w = b.width * 0.5 * ratio;

    // wobble travels toward the tip. the glow is smoother than the core
    float n = BeamFbm(float2((d - b.scroll) * b.noiseScale, b.seed + layer * 3.1));
    float wob = 1.0 + b.noiseStrength * (n * 2.0 - 1.0) * ((layer == 0) ? 0.3 : 0.6);
    w *= wob;

    // thin out toward the tip, open up from the root
    float tip = saturate((len - d) / max(b.tipFade, 0.01));
    float root = saturate(d / max(b.rootFade, 0.01));
    w *= lerp(0.35, 1.0, tip) * lerp(0.6, 1.0, root);
    return w;
}

#endif
