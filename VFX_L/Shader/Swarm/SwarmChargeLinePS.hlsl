// ============================================================
// SwarmChargeLinePS.hlsl
// 突撃兵の予告の帯（SwarmChargeLineVS、2026-10-08）。
// uv.x は 0（突撃兵）.. 1（先端）、uv.y は横方向に -1 .. 1。
// 両脇に縁、塗りは溜めに合わせて先端へ伸び、
// CHEVRON_STEP m ごとの山形 ">" が突進の向きを示す。
// 出力は乗算済み: CommonStates::AlphaBlend は ONE / INV_SRC_ALPHA。
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
static const float CHEVRON_SLANT = 0.5; // 腕が先端からどれだけ後ろに下がるか（帯の半幅単位）
static const float CHEVRON_WIDTH = 0.28;

float4 main(LineIn i) : SV_TARGET
{
    float aaX = max(fwidth(i.uv.x), 1e-4);
    float aaY = max(fwidth(i.uv.y), 1e-4);

    // 突撃兵の側でフェードイン、先端はくっきり
    float ends = smoothstep(0.0, 0.04, i.uv.x) * (1.0 - smoothstep(1.0 - aaX, 1.0, i.uv.x));
    float fill = 1.0 - smoothstep(i.progress - aaX, i.progress, i.uv.x);
    float rim = smoothstep(1.0 - i.size.y - aaY, 1.0 - i.size.y, abs(i.uv.y));

    // 山形 ">": 腕（|uv.y| = 1）は中心線より CHEVRON_SLANT だけ後ろに下がる
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
