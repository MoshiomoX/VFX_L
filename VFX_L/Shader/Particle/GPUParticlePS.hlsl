// ============================================
// ParticlePS.hlsl
// 粒子描画PS — テクスチャサンプリング + 色出力
// ============================================

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    // テクスチャサンプリング
    float4 texColor = g_Texture.Sample(g_Sampler, input.uv);

    // 頂点色と乗算
    float4 finalColor = texColor * input.color;

    // アルファが極小なら描画しない
    clip(finalColor.a - 0.01);

    return finalColor;
}