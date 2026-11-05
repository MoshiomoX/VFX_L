// ============================================================
// SSAOBlurPS.hlsl
// 生の SSAO（SSAOPS、2026-11-02）を深度を見て 5x5 のボックスぼかしにかける。
// ピクセルごとのカーネル回転が残す細かい模様を、このぼかしでならす。
// 視点からの深度が中心と一定割合以上違う近傍は除外するので、AO が
// シルエットをまたいで滲まない
// （敵の縁が後ろの地面に乗るなど）。
// 全画面三角形は同じもの（Sky/SkyVS）。陰影計算が読む R8 に書く。
// ============================================================

Texture2D<float2> g_AO : register(t0);   // x = ao、y = 視点からの深度（そのまま通す）
Texture2DMS<float> g_Depth : register(t1);

cbuffer SSAOBlurCB : register(b0)
{
    float depthA;       // 視点からの深度 = depthB / (d - depthA)
    float depthB;
    float depthTolerance;   // 近傍が中心の深度からずれてよい割合（0.05 = 5 %）
    float pixelScale;       // AO の 1 画素 = 深度の何画素（縦横半分で作る時 = 2、2026-11-05）
};

float EyeDepth(int2 p, int2 size)
{
    p = clamp(p, int2(0, 0), size - 1);
    float d = g_Depth.Load(p, 0);
    return depthB / (d - depthA);
}

float4 main(float4 pos : SV_POSITION) : SV_Target
{
    int2 size;
    int samples;
    g_Depth.GetDimensions(size.x, size.y, samples);
    uint aoW, aoH;
    g_AO.GetDimensions(aoW, aoH);
    int2 aoSize = int2(aoW, aoH);
    int s = (int)pixelScale;
    int2 p = int2(pos.xy);              // AO の画素
    float zc = EyeDepth(p * s, size);   // SSAOPS と同じ代表の深度
    float tol = max(zc * depthTolerance, 0.02);

    float sum = 0.0;
    float wsum = 0.0;
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            int2 q = clamp(p + int2(x, y), int2(0, 0), aoSize - 1);
            float w = (abs(EyeDepth(q * s, size) - zc) < tol) ? 1.0 : 0.0;
            sum += g_AO.Load(int3(q, 0)).x * w;
            wsum += w;
        }
    }
    float ao = (wsum > 0.0) ? sum / wsum : g_AO.Load(int3(p, 0)).x;
    return float4(ao, zc, 0.0, 1.0);
}
