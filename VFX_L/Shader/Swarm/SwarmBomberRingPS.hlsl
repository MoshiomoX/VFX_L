// ============================================================
// SwarmBomberRingPS.hlsl
// Rim + growing disc of the bomber warning ring (SwarmBomberRingVS).
// uv is -1..1 across the blast radius, so r = 1 is the rim.
// Output is premultiplied: CommonStates::AlphaBlend is ONE / INV_SRC_ALPHA.
// Same cbuffer as the VS (SwarmBomberRingCB, b0).
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

struct RingIn
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float progress : TEXCOORD1;
    float edgeWidth : TEXCOORD2;
};

float4 main(RingIn i) : SV_TARGET
{
    float r = length(i.uv);
    float aa = max(fwidth(r), 1e-4); // one pixel, keeps the edges smooth at any distance

    float inside = 1.0 - smoothstep(1.0 - aa, 1.0, r);
    if (inside <= 0.0)
        discard;

    float fill = 1.0 - smoothstep(i.progress - aa, i.progress, r);
    float rim = smoothstep(1.0 - i.edgeWidth - aa, 1.0 - i.edgeWidth, r);

    float4 c = lerp(g_RingBack, g_RingFill, fill);
    c = lerp(c, g_RingEdge, rim);
    c.a *= inside;
    return float4(c.rgb * c.a, c.a);
}
