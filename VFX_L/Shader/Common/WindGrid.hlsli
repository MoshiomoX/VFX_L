// ============================================================
// WindGrid.hlsli
// プレイヤーに付いて動く風の速さの格子（World/WindGrid）を引く。
// 風源の表（WindSources.hlsli）を格子へ足し、拡散・移流・減衰させるので、
// 突風が止んでも少し風が残る。
// 使い方：WindSourceAir（式の風）を出してから WindGridBlend(式の風, 位置) で格子の風に置き換える。
// 格子の外・縁・上の方では式の風へ戻る。格子が無い（未バインド）時は式の風のまま。
// g_WindGridInfo[0] = (原点 xyz, 1 マスの m)、[1] = (マス数 xyz, 1 = 有効)、[2] = (縁で薄める m, 上で薄めるマス, 足元の高さ, -)
// レジスタは指定しない（読む側のシェーダーごとに空いている所。C++ は名前で結ぶ）
// ============================================================
#ifndef WIND_GRID_HLSLI
#define WIND_GRID_HLSLI

Texture3D<float4> g_WindGrid;
StructuredBuffer<float4> g_WindGridInfo;

// マスの中心が (i + 0.5) の位置。f = 格子の座標（マス単位）。範囲外は端のマスを使う
float3 WindGridSampleTex(Texture3D<float4> tex, float3 f, int3 dims)
{
    f -= 0.5;
    int3 i0 = (int3)floor(f);
    float3 t = f - (float3)i0;
    int3 a = clamp(i0, int3(0, 0, 0), dims - 1);
    int3 b = clamp(i0 + 1, int3(0, 0, 0), dims - 1);
    float3 c000 = tex.Load(int4(a.x, a.y, a.z, 0)).xyz;
    float3 c100 = tex.Load(int4(b.x, a.y, a.z, 0)).xyz;
    float3 c010 = tex.Load(int4(a.x, b.y, a.z, 0)).xyz;
    float3 c110 = tex.Load(int4(b.x, b.y, a.z, 0)).xyz;
    float3 c001 = tex.Load(int4(a.x, a.y, b.z, 0)).xyz;
    float3 c101 = tex.Load(int4(b.x, a.y, b.z, 0)).xyz;
    float3 c011 = tex.Load(int4(a.x, b.y, b.z, 0)).xyz;
    float3 c111 = tex.Load(int4(b.x, b.y, b.z, 0)).xyz;
    float3 x00 = lerp(c000, c100, t.x), x10 = lerp(c010, c110, t.x);
    float3 x01 = lerp(c001, c101, t.x), x11 = lerp(c011, c111, t.x);
    return lerp(lerp(x00, x10, t.y), lerp(x01, x11, t.y), t.z);
}

// 式の風 formulaAir を、pos での格子の風に置き換える（縁・上の方では式の風と混ぜる）
float3 WindGridBlend(float3 formulaAir, float3 pos)
{
    float4 i0 = g_WindGridInfo[0];
    float4 i1 = g_WindGridInfo[1];
    float4 i2 = g_WindGridInfo[2];
    float3 result = formulaAir;
    if (i1.w > 0.5 && i0.w > 0.0)
    {
        float3 dims = i1.xyz;
        float3 f = (pos - i0.xyz) / i0.w;
        float edge = max(i2.x / i0.w, 1e-3);
        float wx = saturate(min(f.x, dims.x - f.x) / edge);
        float wz = saturate(min(f.z, dims.z - f.z) / edge);
        float wy = saturate((dims.y - f.y) / max(i2.y, 1e-3));   // 下は一番下のマスを使う
        float w = wx * wz * wy;
        if (w > 0.0)
        {
            // 風源の中は式の風の方が強い（格子は 1m のマスと拡散で芯の鋭さが 4 割ほど落ちる）→ 強い方を使う。
            // 風源の外（式の風 0）では格子の残った風・流れてきた風が出る
            float3 g = WindGridSampleTex(g_WindGrid, f, (int3)dims);
            float3 pick = (dot(formulaAir, formulaAir) >= dot(g, g)) ? formulaAir : g;
            result = lerp(formulaAir, pick, w);
        }
    }
    return result;
}

// 高さを持たない読み手（地面の 2D の図：草の踏み跡・水面・砂の面・瘴気）。足元 + 1m の高さで引く
float3 WindGridBlend2D(float3 formulaAir, float2 xz)
{
    return WindGridBlend(formulaAir, float3(xz.x, g_WindGridInfo[2].z + 1.0, xz.y));
}

#endif
