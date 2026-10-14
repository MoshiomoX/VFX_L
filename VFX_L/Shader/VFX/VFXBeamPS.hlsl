// ============================================================
// VFXBeamPS.hlsl
// Cross-section profile per layer x flowing streaks along the beam.
// Written premultiplied with alpha 0 = additive under the
// ONE / INV_SRC_ALPHA blend (same as additive particles). HDR colours
// feed the bloom.
// ============================================================
#include "../Common/BeamCommon.hlsli"

StructuredBuffer<BeamItem> g_Beams : register(t0);

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint beam : TEXCOORD1;
    nointerpolation uint layer : TEXCOORD2;
    nointerpolation float len : TEXCOORD3;
};

float4 main(PSIn i) : SV_TARGET
{
    BeamItem b = g_Beams[i.beam];
    float d = i.uv.x;
    float y = abs(i.uv.y);
    float r = saturate(1.0 - y * y); // 1 at the centre line, 0 at the edge
    float n = BeamFbm(float2((d - b.scroll) * b.noiseScale, i.uv.y * 1.3 + b.seed));
    // gradients for the toon edges, taken outside the branches
    float rW = max(fwidth(r), 1e-3);
    float nW = max(fwidth(n), 1e-3);
    bool toon = b.toon > 0.5;

    // ---- cross-section ----
    // toon: every layer is a flat band with a crisp edge (glow, main and core stack into
    // three clear steps toward the centre) instead of a smooth falloff
    float a;
    float3 col;
    if (i.layer == 0)
    {
        a = (toon ? smoothstep(0.30 - rW, 0.30 + rW, r) : r * r) * b.glowAlpha;
        col = b.color.rgb * 0.7;
    }
    else if (i.layer == 1)
    {
        a = toon ? smoothstep(0.35 - rW, 0.35 + rW, r) : pow(r, 1.6);
        col = b.color.rgb;
    }
    else
    {
        a = toon ? smoothstep(0.45 - rW, 0.45 + rW, r) : pow(r, 0.8);
        col = b.coreColor.rgb;
    }

    // ---- along: root fade in, tip fade out ----
    float root = saturate(d / max(b.rootFade, 0.01));
    float tip = saturate((i.len - d) / max(b.tipFade, 0.01));
    a *= root * tip;

    // ---- flowing streaks (toward the tip). the core keeps most of its brightness ----
    // toon: two levels with a crisp border instead of a smooth ramp
    float nn = toon ? smoothstep(0.55 - nW, 0.55 + nW, n) : n;
    float streak = lerp(1.0 - b.noiseStrength, 1.0 + b.noiseStrength * 0.7, nn);
    if (i.layer == 2) streak = lerp(1.0, streak, 0.35);
    a *= streak;

    a *= (i.layer == 2) ? b.coreColor.a : b.color.a;
    clip(a - 0.002);
    return float4(col * a, 0.0);
}
