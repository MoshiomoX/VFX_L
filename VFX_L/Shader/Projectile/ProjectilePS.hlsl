// ============================================================
// ProjectilePS.hlsl
// 投射物の芯。加算合成前提なので色をそのまま出す。
// ============================================================
Texture2D coreTexture : register(t0);
SamplerState coreSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 tex = coreTexture.Sample(coreSampler, input.uv);
    return tex * input.color;
}