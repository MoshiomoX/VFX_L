// ============================================================
// CompositePS.hlsl
// シーン + ブルーム -> 露出 -> （トーンマップ）-> ガンマ -> バックバッファ。
// パイプラインの中でリニア HDR を抜けるのはここだけ。
// ============================================================
Texture2D sceneTex : register(t0);
Texture2D bloomTex : register(t1);
// 画面の歪み（2026-10-20）: パーティクル（ParticleDistortPS）が書いた uv のずれ、リゾルブ済み。
// 予備の経路でしかない: 普段は UI を描く前に Graphics::ApplyDistortion（DistortApplyPS）がシーンを
// もう歪ませてフラグを下ろしているので、ここでの g_DistortStrength は 0。DistortApplyPS の読み込みに
// 失敗したときだけこの経路が画像を歪ませる（そのときは HDR ターゲットに描いた UI も一緒に歪む）
Texture2D distortTex : register(t2);
SamplerState linearClamp : register(s0);

cbuffer CompositeCB : register(b0)
{
    float g_BloomIntensity;
    float g_Exposure;
    uint g_Tonemap; // 0 = オフ、1 = ACES
    float g_Gamma; // 表示のガンマ指数（2.2 = sRGB 相当）。<= 0 = オフ
    float g_DistortStrength; // 0 = このフレームは歪みなし
    float g_Aspect;          // 幅 / 高さ（ずれは画面上で縦横同じ大きさ）
    float2 _cpad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 ACES(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(PSInput i) : SV_TARGET
{
    float2 uv = i.uv;
    if (g_DistortStrength > 0.0)
    {
        float2 d = distortTex.Sample(linearClamp, i.uv).xy * g_DistortStrength;
        d.y *= g_Aspect;   // 縦横どちらも同じピクセル距離にする
        uv = saturate(i.uv + d);
    }
    float3 scene = sceneTex.Sample(linearClamp, uv).rgb;
    float3 bloom = bloomTex.Sample(linearClamp, uv).rgb;

    float3 c = (scene + bloom * g_BloomIntensity) * g_Exposure;

    if (g_Tonemap != 0u)
        c = ACES(c);
    if (g_Gamma > 0.0)
        c = pow(max(c, 0.0), 1.0 / g_Gamma);

    return float4(c, 1.0);
}