// ============================================================
// SwarmChargeLinePS.hlsl
// The charger's warning band (SwarmChargeLineVS, 2026-10-08).
// uv.x runs 0 (the charger) .. 1 (the tip), uv.y -1 .. 1 across.
// Rim along both sides, the fill grows toward the tip with the wind-up,
// chevrons ">" every CHEVRON_STEP m point the way the dash goes.
// Output is premultiplied: CommonStates::AlphaBlend is ONE / INV_SRC_ALPHA.
// ============================================================
cbuffer SwarmBomberRingCB : register(b0)
{
    row_major float4x4 g_RingView;
    row_major float4x4 g_RingProj;
    float4 g_RingFill;
    float4 g_RingEdge;
    float4 g_RingBack;
    float g_RingEdgeWidth;
    float g_RingLift;
    float2 _ringPad;
};

struct LineIn
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float progress : TEXCOORD1;
    float2 size : TEXCOORD2;
};

static const float CHEVRON_STEP = 1.6;  // m
static const float CHEVRON_SLANT = 0.5; // how far the arms trail behind the point, in band half widths
static const float CHEVRON_WIDTH = 0.28;

float4 main(LineIn i) : SV_TARGET
{
    float aaX = max(fwidth(i.uv.x), 1e-4);
    float aaY = max(fwidth(i.uv.y), 1e-4);

    // fade in at the charger, sharp at the tip
    float ends = smoothstep(0.0, 0.04, i.uv.x) * (1.0 - smoothstep(1.0 - aaX, 1.0, i.uv.x));
    float fill = 1.0 - smoothstep(i.progress - aaX, i.progress, i.uv.x);
    float rim = smoothstep(1.0 - i.size.y - aaY, 1.0 - i.size.y, abs(i.uv.y));

    // ">" chevrons: the arms (|uv.y| = 1) trail CHEVRON_SLANT behind the centre line
    float m = (i.uv.x * i.size.x + abs(i.uv.y) * CHEVRON_SLANT) / CHEVRON_STEP;
    float fm = frac(m);
    float aaM = max(fwidth(m), 1e-4);
    float chev = smoothstep(0.0, aaM, fm) * (1.0 - smoothstep(CHEVRON_WIDTH - aaM, CHEVRON_WIDTH, fm));

    float4 c = lerp(g_RingBack, g_RingFill, fill);
    c = lerp(c, g_RingEdge, chev * 0.6);
    c = lerp(c, g_RingEdge, rim);
    c.a *= ends;
    return float4(c.rgb * c.a, c.a);
}
