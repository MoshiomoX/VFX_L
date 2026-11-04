// ============================================================
// SwarmEnemyHpBarPS.hlsl
// 雑魚の HP バーの枠 / 中身 / 空の背景。四角は
// SwarmEnemyHpBarVS が作り、中身はピクセルごとに
// uv.x と残り hp の割合を比べて決める。
// cbuffer は VS と同じ（SwarmHpBarCB、b0）。
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
    // 枠の太さ（uv 単位。4 辺ともワールドで同じ太さ）
    float2 edge = float2(g_BarBorder / max(g_BarWidth, 1e-4),
                         g_BarBorder / max(g_BarHeight, 1e-4));
    if (any(i.uv < edge) || any(i.uv > 1.0 - edge))
        return g_BarEdge;

    // 枠の内側での位置、0..1
    float x = (i.uv.x - edge.x) / max(1.0 - 2.0 * edge.x, 1e-4);
    return (x <= i.ratio) ? g_BarFill : g_BarBack;
}
