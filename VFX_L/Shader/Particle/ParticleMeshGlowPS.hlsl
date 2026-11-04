// ============================================
// ParticleMeshGlowPS.hlsl
// メッシュパーティクルの発光パス（加算、ライティングなし、深度は読むだけ）。
// 色 = モデルのアルベド x パーティクルの色。パーティクルの色のアルファ
// （寿命に応じた色）で消えていく。合成ステートは
// SRC_ALPHA / ONE なので、ここでは色を乗算済みにしない。
// ParticleMeshVS と対で使う。
// 「後ろ」（PARTICLE_MESH_BEHIND、VS から Tangent.x = 1）: 裏面だけを
// 描くので、光は不透明な本体の後ろに来て、その周りの縁として見える。
// 発光パスはカリングなしでラスタライズするので、表裏は
// ここで判定する。
// ============================================
#include "../Common/ModelCommon.hlsli"

float4 main(PS_INPUT input, bool isFront : SV_IsFrontFace) : SV_TARGET
{
    if (input.Tangent.x > 0.5 && isFront)
        discard;
    float4 tex = albedoTexture.Sample(samplerState, input.UV);
    return tex * input.Color;
}
