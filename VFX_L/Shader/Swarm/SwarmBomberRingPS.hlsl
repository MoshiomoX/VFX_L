// ============================================================
// SwarmBomberRingPS.hlsl
// 自爆兵の警告円（SwarmBomberRingVS）の縁 + 大きくなる円盤。
// uv は爆発半径の端から端までで -1..1 なので、r = 1 が縁。
// 出力は乗算済み：CommonStates::AlphaBlend は ONE / INV_SRC_ALPHA。
// cbuffer は VS と同じ（SwarmBomberRingCB、b0）。
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
    // x = 1：警告の扇形 / 帯（SwarmWarnRingVS、2026-10-25）：塗りは r ではなく y（どこまで進んだか）に沿って広がる。
    // 丸い円はすべて 0（自爆兵 / メテオの VS はゼロのまま）
    float2 shapeFill : TEXCOORD3;
};

float4 main(RingIn i) : SV_TARGET
{
    float r = length(i.uv);
    float aa = max(fwidth(r), 1e-4); // 1 ピクセル分。どの距離でも縁をなめらかに保つ
    float fr = (i.shapeFill.x > 0.5) ? i.shapeFill.y : r;   // 塗りが広がっていく軸
    float aaFill = max(fwidth(fr), 1e-4);

    float inside = 1.0 - smoothstep(1.0 - aa, 1.0, r);
    if (inside <= 0.0)
        discard;

    float fill = 1.0 - smoothstep(i.progress - aaFill, i.progress, fr);
    float rim = smoothstep(1.0 - i.edgeWidth - aa, 1.0 - i.edgeWidth, r);

    // progress が負（警告円のみ、2026-10-07）= 縁だけ：縁の内側には何も描かない
    float4 back = (i.progress < 0.0) ? float4(0, 0, 0, 0) : g_RingBack;
    float4 c = lerp(back, g_RingFill, fill);
    c = lerp(c, g_RingEdge, rim);
    c.a *= inside;
    return float4(c.rgb * c.a, c.a);
}
