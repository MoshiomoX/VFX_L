// ============================================================
// SpritePS.hlsl
// 2D スプライト。テクスチャ色 × 乗算色。
// ※素材自体に色が付いている場合、乗算なので暗く寄る点に注意。
// ============================================================
Texture2D spriteTexture : register(t0);
SamplerState spriteSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return spriteTexture.Sample(spriteSampler, input.uv) * input.color;
}