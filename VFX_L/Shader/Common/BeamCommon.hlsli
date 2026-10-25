// ============================================================
// BeamCommon.hlsli
// Shared by VFXBeamVS / VFXBeamPS: the beam item (C++ VFXBeamItem, 112 bytes),
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
    // lightning (2026-10-15): the ribbon kinks sideways at every segment
    float jitter; // large sway amplitude (m). 0 = straight beam
    float jitterRate; // shapes per second (re-rolled)
    float jitterFine; // per-segment kink amplitude (fraction of jitter)
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

// lightning phase: which random shape is shown right now
float BeamLightningPhase(BeamItem b, float time)
{
    return floor(time * max(b.jitterRate, 1.0)) * 7.31 + b.seed * 13.7;
}

// sideways offset of the segment boundary `boundary` (0..BEAM_SEGMENTS) at
// t (0..1 along the beam). Piecewise linear between boundaries, so the
// bolt has sharp kinks. Pinned at both ends. Both quads sharing a boundary
// get the same offset (same boundary index), so there are no cracks
float3 BeamLightningOffset(BeamItem b, float boundary, float t, float3 axis, float phase)
{
    float3 up = (abs(axis.y) < 0.99) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 n1 = normalize(cross(axis, up));
    float3 n2 = cross(axis, n1);
    // fine kink at every boundary
    float2 f = float2(BeamHash(float2(boundary, phase)), BeamHash(float2(boundary + 57.0, phase))) * 2.0 - 1.0;
    // coarse sway: one value every 4 boundaries, interpolated
    float cb = boundary * 0.25;
    float ci = floor(cb);
    float cf = frac(cb);
    float2 c0 = float2(BeamHash(float2(ci * 3.0 + 101.0, phase)), BeamHash(float2(ci * 3.0 + 151.0, phase))) * 2.0 - 1.0;
    float2 c1 = float2(BeamHash(float2(ci * 3.0 + 104.0, phase)), BeamHash(float2(ci * 3.0 + 154.0, phase))) * 2.0 - 1.0;
    float2 c = lerp(c0, c1, cf);
    float2 off = (f * b.jitterFine + c) * b.jitter;
    float env = saturate(t * 10.0) * saturate((1.0 - t) * 10.0);
    return (n1 * off.x + n2 * off.y) * env;
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
