// ============================================================
// BloomCS.hlsl
// 1 つのシェーダーで 3 段階、g_Mode で選ぶ（一様な分岐）：
//   0  prefilter  : フル解像度のシーン -> 半分の解像度、ソフトな閾値
//   1  downsample : デュアルフィルタ（Kawase）5 タップ、半分の大きさへ
//   2  upsample   : 小さい方の mip をデュアルフィルタ 8 タップで読み、
//                   同じ大きさのダウンサンプル mip の上に足す
//
// 「出力先」のテクセル 1 つにつき 1 スレッド。入力は
// リニア・クランプのサンプラーで読むので、出力先テクセルの中心での
// 1 タップで既に入力の 2x2 テクセルの平均になる。
// ============================================================
Texture2D<float4> src : register(t0); // mode 0：シーン、1：大きい方の mip、2：小さい方の mip
Texture2D<float4> srcBase : register(t1); // mode 2 のみ：dst と同じ大きさ
SamplerState linearClamp : register(s0);
RWTexture2D<float4> dst : register(u0);

cbuffer BloomCB : register(b0)
{
    float2 g_DstTexel; // 1 / dst の大きさ
    float2 g_SrcTexel; // 1 / src の大きさ
    float g_Threshold;
    float g_Knee;
    uint g_Mode;
    float _pad;
};

float3 Prefilter(float2 uv)
{
    float3 c = src.SampleLevel(linearClamp, uv, 0).rgb;

    // ソフトニー：(threshold - knee) から threshold にかけて徐々に効く
    float br = max(c.r, max(c.g, c.b));
    float soft = br - g_Threshold + g_Knee;
    soft = clamp(soft, 0.0, 2.0 * g_Knee);
    soft = soft * soft / (4.0 * g_Knee + 1e-5);
    float contrib = max(soft, br - g_Threshold) / max(br, 1e-5);
    return c * contrib;
}

float3 Downsample(float2 uv)
{
    float2 o = g_SrcTexel;
    float3 sum = src.SampleLevel(linearClamp, uv, 0).rgb * 4.0;
    sum += src.SampleLevel(linearClamp, uv + float2(-o.x, -o.y), 0).rgb;
    sum += src.SampleLevel(linearClamp, uv + float2(o.x, -o.y), 0).rgb;
    sum += src.SampleLevel(linearClamp, uv + float2(-o.x, o.y), 0).rgb;
    sum += src.SampleLevel(linearClamp, uv + float2(o.x, o.y), 0).rgb;
    return sum / 8.0;
}

float3 Upsample(float2 uv)
{
    float2 o = g_SrcTexel * 0.5;
    float3 sum = 0;
    sum += src.SampleLevel(linearClamp, uv + float2(-2.0 * o.x, 0.0), 0).rgb;
    sum += src.SampleLevel(linearClamp, uv + float2(2.0 * o.x, 0.0), 0).rgb;
    sum += src.SampleLevel(linearClamp, uv + float2(0.0, -2.0 * o.y), 0).rgb;
    sum += src.SampleLevel(linearClamp, uv + float2(0.0, 2.0 * o.y), 0).rgb;
    sum += src.SampleLevel(linearClamp, uv + float2(-o.x, -o.y), 0).rgb * 2.0;
    sum += src.SampleLevel(linearClamp, uv + float2(o.x, -o.y), 0).rgb * 2.0;
    sum += src.SampleLevel(linearClamp, uv + float2(-o.x, o.y), 0).rgb * 2.0;
    sum += src.SampleLevel(linearClamp, uv + float2(o.x, o.y), 0).rgb * 2.0;

    float3 base = srcBase.SampleLevel(linearClamp, uv, 0).rgb;
    return base + sum / 12.0;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    dst.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;

    float2 uv = (id.xy + 0.5) * g_DstTexel;

    float3 c;
    if (g_Mode == 0u)
        c = Prefilter(uv);
    else if (g_Mode == 1u)
        c = Downsample(uv);
    else
        c = Upsample(uv);

    dst[id.xy] = float4(c, 1.0);
}