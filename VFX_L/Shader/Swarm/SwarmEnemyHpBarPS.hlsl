// ============================================================
// SwarmEnemyHpBarPS.hlsl
// Border / fill / empty background of the mob HP bar. The quad
// comes from SwarmEnemyHpBarVS; the fill is decided per pixel
// from uv.x against the remaining-hp ratio.
// Same cbuffer as the VS (SwarmHpBarCB, b0).
// ============================================================
cbuffer SwarmHpBarCB : register(b0)
{
    row_major float4x4 g_BarView;
    row_major float4x4 g_BarProj;
    float g_BarWidth;
    float g_BarHeight;
    float g_BarOffset;
    float g_BarBorder;
    float4 g_BarFill;
    float4 g_BarBack;
    float4 g_BarEdge;
};

struct BarIn
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float ratio : TEXCOORD1;
};

float4 main(BarIn i) : SV_TARGET
{
    // border thickness in uv units (same world thickness on all four sides)
    float2 edge = float2(g_BarBorder / max(g_BarWidth, 1e-4),
                         g_BarBorder / max(g_BarHeight, 1e-4));
    if (any(i.uv < edge) || any(i.uv > 1.0 - edge))
        return g_BarEdge;

    // position inside the border, 0..1
    float x = (i.uv.x - edge.x) / max(1.0 - 2.0 * edge.x, 1e-4);
    return (x <= i.ratio) ? g_BarFill : g_BarBack;
}
