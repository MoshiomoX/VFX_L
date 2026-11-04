// ============================================================
// OutlinePS.hlsl
// シーンの深度から作るスクリーン空間の墨線（トゥーン調、2026-10-04）
// + ステンシルから作る陣営の線（2026-10-06：味方は青で少し太く、
// 敵は赤）。
// 全画面三角形（Sky/SkyVS）で、不透明なシーンの後・
// 草や半透明物の前に、乗算済みアルファのブレンド
// （out + dest * (1 - a)）で描く：下の色が k だけ線の色へ引き寄せられる。
// 暗い既定の tint なら昔の乗算と同じに見える。
//
// エッジ判定：平面なら 1/viewZ は画面上で線形なので、向かい合う近傍
// 2 点の組ごとに 1/z で中心の深度を予測する。その予測より
// （自分の深度に対して）はっきり「手前」にあるピクセルは、
// 手前の面のシルエットか凸の折れ目の上にある。
// 線を引くのは手前側だけ（くっきり、段 1 つにつき 1 ピクセル幅）。
//
// 陣営：ステンシルには味方 1 / 敵 2 が、描いた所に入っている
// （RenderSystem / SwarmSystem が書く）。陣営を持つピクセルはその陣営の
// 色と太さを使い、さらに近傍のステンシルが違う所はすべて線にする
// （深度の段が小さい所、例えば地面に着いた足でもシルエット全体が
// 閉じるように）。
// ============================================================

Texture2DMS<float> g_Depth : register(t0);
Texture2DMS<uint2> g_Stencil : register(t1);   // X24_TYPELESS_G8_UINT：ステンシルは .g

cbuffer OutlineCB : register(b0)
{
    float depthA;       // 視点からの深度 = depthB / (d - depthA)（射影行列から）
    float depthB;
    float thickness;    // 近傍までの距離（ピクセル、>= 1）
    float threshold;    // 線にする深度の段（相対値、0.03 = 深度の 3 %）
    float fadeStart;    // m：この間で線が消えていく
    float fadeEnd;
    float strength;     // 0..1
    float debugView;    // 1 = 視点からの深度を縞模様で見せる（調整用）
    float3 lineTint;    // 中立の線の色（リニア）
    float factionStrength;   // 0 = 陣営の線なし
    float3 friendColor;
    float friendThickness;   // ピクセル
    float3 enemyColor;
    float enemyThickness;
};

float EyeDepth(int2 p, int2 size)
{
    p = clamp(p, int2(0, 0), size - 1);
    float d = g_Depth.Load(p, 0);
    return depthB / (d - depthA);
}

uint Faction(int2 p, int2 size)
{
    p = clamp(p, int2(0, 0), size - 1);
    return g_Stencil.Load(p, 0).g;
}

float4 main(float4 pos : SV_POSITION) : SV_Target
{
    int2 size;
    int samples;
    g_Depth.GetDimensions(size.x, size.y, samples);

    int2 p = int2(pos.xy);
    float zc = EyeDepth(p, size);
    if (debugView > 0.5)
        return float4(frac(zc * 0.25), frac(zc * 0.05), 1.0, 1.0);

    // ---- どの線か：中立・味方・敵（このピクセルの下のステンシルで決める） ----
    uint f = (factionStrength > 0.0) ? Faction(p, size) : 0u;
    float3 color = lineTint;
    float  px = thickness;
    if (f == 1u) { color = friendColor; px = friendThickness; }
    else if (f == 2u) { color = enemyColor; px = enemyThickness; }
    int t = max(1, (int)(px + 0.5));

    float edge = 0.0;
    float fedge = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        int2 o = (i == 0) ? int2(1, 0) : (i == 1) ? int2(0, 1) : (i == 2) ? int2(1, 1) : int2(1, -1);
        o *= t;
        float za = EyeDepth(p + o, size);
        float zb = EyeDepth(p - o, size);
        float zp = 2.0 / (1.0 / za + 1.0 / zb);   // 近傍が作る平面の、中心での深度
        float front = (zp - zc) / zc;              // > 0：中心がその平面より手前
        edge = max(edge, smoothstep(threshold, threshold * 2.0, front));
        if (f != 0u)
        {
            // 陣営を持つもののシルエット：近傍が同じ陣営ではない
            if (Faction(p + o, size) != f || Faction(p - o, size) != f) fedge = 1.0;
        }
    }

    float k;
    if (f != 0u)
    {
        // 陣営の線は距離で消さない（遠くの敵も赤だと分かるように）
        k = saturate(max(edge, fedge) * factionStrength);
    }
    else
    {
        float fade = 1.0 - smoothstep(fadeStart, max(fadeEnd, fadeStart + 1e-3), zc);
        k = saturate(edge * strength * fade);
    }
    return float4(color * k, k);
}