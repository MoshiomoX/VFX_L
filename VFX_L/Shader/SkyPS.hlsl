// ============================================================
// SkyPS.hlsl
// スカイボックス用 ピクセルシェーダー
// パノラマテクスチャをそのまま表示（ライティング無し）
// ============================================================
Texture2D skyTexture : register(t0);
SamplerState samplerState : register(s0);

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    return skyTexture.Sample(samplerState, input.UV);
}