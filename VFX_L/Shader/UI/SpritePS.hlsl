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
    // SpriteRenderer は乗算済みアルファ（ONE / INV_SRC_ALPHA）で描くので、
    // ここでアルファを掛けておく。掛けないと半透明の色がブレンドされずに
    // 上へ加算される（alpha 0.06 の白が真っ白になった）
    float4 c = spriteTexture.Sample(spriteSampler, input.uv) * input.color;
    return float4(c.rgb * c.a, c.a);
}