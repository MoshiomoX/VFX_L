// ============================================================
// SandFootprintPS.hlsl
// 砂に押された靴跡。既にある色への乗数として描く
// （ブレンド：dest x src。なので砂の色とトゥーンのライティングは
// 残る。src > 1 で明るく、< 1 で暗くなる）。
//   板の上の高さ場：靴底（前足部、かかと、土踏まずを
//   なめらかに合成したもの）がへこみで、前足部、かかと、
//   蹴り出すつま先の下が一番深い。その周りに押しのけられた砂の低い盛り上がり、
//   つま先の前ほど大きい。その高さの傾きを本物の地面のように
//   太陽で照らす：太陽を向いた壁は明るく、反対側の壁は
//   影になる。踏み固めた底は少し暗く、ワールド空間の砂粒の
//   ノイズでむらを付ける。
// ============================================================
#include "SandFootprintCommon.hlsli"

float Ellipse(float2 p, float2 c, float2 r)
{
    return length((p - c) / r) - 1.0;
}

float SMin(float a, float b, float k)
{
    float h = saturate(0.5 + 0.5 * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0 - h);
}

float SoleDist(float2 p, out float ball, out float heel)
{
    // 靴底：幅の広い前足部、長いかかと、土踏まずは内側が少しくびれる程度
    ball = Ellipse(p, float2(0.02, 0.34), float2(0.92, 0.64));
    heel = Ellipse(p, float2(-0.01, -0.54), float2(0.74, 0.46));
    float arch = Ellipse(p, float2(-0.06, -0.10), float2(0.72, 0.60));
    return SMin(SMin(ball, heel, 0.45), arch, 0.40);
}

// へこみのどれだけ内側か（縁で 0、底で 1）。壁は広め：緩い砂が崩れ込む
float Inside(float d)
{
    return smoothstep(-0.04, 0.55, -d);
}

// x = へこみ（g_Depth 単位、負）、y = 盛り上がり（g_Berm 単位）
float2 Height(float2 p)
{
    float ball, heel;
    float d = SoleDist(p, ball, heel);
    float inside = Inside(d);
    float press = max(saturate(-ball * 1.4), saturate(-heel * 1.4));
    float toe = saturate((p.y - 0.30) * 1.6) * saturate(-ball * 1.8);
    // 底はほぼ平ら（でこぼこの底は明るい斑点に見えた）。蹴り出すつま先だけ少し深く掘る
    float dent = -inside * (0.85 + 0.05 * press + 0.15 * toe);
    // 盛り上がり：縁のすぐ外の輪、前ほど高い（走るとつま先が砂を前へ押し出す）
    float x = (d - 0.12) / 0.13;
    float ring = exp(-x * x);
    float front = 0.5 + 0.8 * saturate(p.y * 0.8);
    return float2(dent, ring * front);
}

float Hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float GrainNoise(float2 m)
{
    // セルノイズ 2 オクターブ、約 1 cm と 2.5 cm の粒
    float a = Hash(floor(m * 90.0));
    float b = Hash(floor(m * 38.0) + 17.0);
    return a * 0.6 + b * 0.4;
}

float4 main(FootOut i) : SV_TARGET
{
    float2 p = i.uv;
    float ball, heel;
    float d = SoleDist(p, ball, heel);
    if (d > 0.40 || i.alpha <= 0.003)
        discard;

    // 高さの傾き（m、靴底空間での差分）
    const float e = 0.05;
    float2 h0 = Height(p);
    float2 hx = Height(p + float2(e, 0.0));
    float2 hy = Height(p + float2(0.0, e));
    float hC = h0.x * g_Depth + h0.y * g_Berm;
    float dhx = (hx.x * g_Depth + hx.y * g_Berm - hC) / (e * i.halfSize.x);
    float dhy = (hy.x * g_Depth + hy.y * g_Berm - hC) / (e * i.halfSize.y);
    float3 n = normalize(float3(-dhx, -dhy, 1.0));

    // 本物の地面のように照らす：周りの平らな砂との比
    float3 L = normalize(i.sun);
    float flat = max(L.z, 0.15);
    float lit = max(dot(n, L), 0.0) / flat;
    float m = lerp(1.0, lit, g_SunGain);

    // 壁は空も少し遮る（太陽が真上でも、へこみは平らな地面より暗い）
    float slope = length(float2(dhx, dhy));
    m *= 1.0 - 0.35 * saturate(slope * 0.8);

    // 踏み固めた底は暗く、緩い盛り上がりはわずかに明るく
    float inside = Inside(d);
    m *= 1.0 - g_FloorDark * inside;
    m *= 1.0 + 0.06 * h0.y;

    // 砂粒（ワールド空間なのでその場に留まる）：砂が乱れた所ほど強く
    float disturbed = saturate(inside + h0.y);
    float g = GrainNoise(i.grainUV) - 0.5;
    m *= 1.0 + g * g_Grain * disturbed;

    // 板の縁でフェードアウトし、次に経過時間 / 風で薄れる。湿った砂（alpha が 1 超）は深く見える
    float fringe = 1.0 - smoothstep(0.25, 0.40, d);
    float k = min(i.alpha, 1.6) * g_Strength * fringe;
    m = lerp(1.0, m, k);
    m = clamp(m, 0.35, 1.6);
    return float4(m, m, m, 1.0);
}
