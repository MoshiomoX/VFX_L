// ============================================================
// BloomCS.hlsl
// One shader, three stages selected by g_Mode (uniform branch):
//   0  prefilter  : full-res scene -> half-res, soft threshold
//   1  downsample : dual-filter (Kawase) 5 taps, to half size
//   2  upsample   : dual-filter 8 taps of the smaller mip, added
//                   on top of the same-size downsample mip
//
// One thread per DESTINATION texel. Sources are read through a
// linear clamp sampler, so a single tap at the destination texel
// center already averages 2x2 source texels.
// ============================================================
Texture2D<float4> src : register(t0); // mode 0: scene, 1: bigger mip, 2: smaller mip
Texture2D<float4> srcBase : register(t1); // mode 2 only: same size as dst
SamplerState linearClamp : register(s0);
RWTexture2D<float4> dst : register(u0);

cbuffer BloomCB : register(b0)
{
    float2 g_DstTexel; // 1 / dst size
    float2 g_SrcTexel; // 1 / src size
    float g_Threshold;
    float g_Knee;
    uint g_Mode;
    float _pad;
};

float3 Prefilter(float2 uv)
{
    float3 c = src.SampleLevel(linearClamp, uv, 0).rgb;

    // soft knee: ramps in from (threshold - knee) to threshold
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