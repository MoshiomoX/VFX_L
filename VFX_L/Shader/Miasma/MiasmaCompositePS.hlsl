// ============================================================
// MiasmaCompositePS.hlsl
// 半解像度の瘴気（MiasmaMarchPS）を画面全体に広げ、
// HDR のシーンに重ねる（乗算済み：out = c + scene x (1 - a)）。
// エッジを考慮する：このピクセルの周りの 4 x 4 の半解像度ピクセルそれぞれの
// 深度（レイを進めた全解像度のピクセル、2p + 1）をこのピクセルの深度と比べ、
// 深度の段差をまたぐタップはほとんど重みを持たないので、壁の手前の
// 霧が壁ににじまない。4 x 4 のテントはレイマーチのジッター模様も
// 平均して消す（TAA は無い）。
// ============================================================

Texture2DMS<float> g_Depth : register(t0);
Texture2D<float4> g_Half : register(t1);

// MiasmaField::CompositeCB（32 バイト）と一致させること
cbuffer MiasmaCompositeCB : register(b0)
{
    float g_DepthA;     // 目からの深度 = g_DepthB / (d - g_DepthA)
    float g_DepthB;
    float g_Tolerance;  // このピクセルの深度に対する割合
    float _pad;
    float2 g_HalfSize;
    float2 g_FullSize;
};

float EyeDepth(int2 p)
{
    p = clamp(p, int2(0, 0), int2(g_FullSize) - 1);
    return g_DepthB / (g_Depth.Load(p, 0) - g_DepthA);
}

float4 main(float4 pos : SV_POSITION) : SV_Target
{
    int2 p = int2(pos.xy);
    float z = EyeDepth(p);
    float2 hc = (float2(p) + 0.5) * 0.5 - 0.5;
    int2 base = int2(floor(hc));
    float2 f = hc - float2(base);
    int2 hmax = int2(g_HalfSize) - 1;
    float tol = max(z * g_Tolerance, 0.02);

    float4 sum = 0.0;
    float wsum = 0.0;
    float4 nearest = 0.0;
    float best = 1e30;
    [unroll]
    for (int y = -1; y <= 2; ++y)
    {
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            int2 hp = clamp(base + int2(x, y), int2(0, 0), hmax);
            float zh = EyeDepth(hp * 2 + 1);
            // 周りの 4 x 4 の半解像度ピクセルにかけるテント（バイリニアより広い：ジッターも隠す）
            float wx = saturate(1.0 - abs(float(x) - f.x) * 0.5);
            float wy = saturate(1.0 - abs(float(y) - f.y) * 0.5);
            float dz = abs(zh - z);
            float wd = exp(-dz / tol);
            float w = wx * wy * wd;
            float4 c = g_Half.Load(int3(hp, 0));
            sum += c * w;
            wsum += w;
            if (dz < best) { best = dz; nearest = c; }
        }
    }
    // すべてのタップが段差の向こう：深度が一番近いものを使う
    return wsum > 1e-4 ? sum / wsum : nearest;
}
