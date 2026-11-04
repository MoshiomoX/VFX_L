// ============================================================
// SkinnedVS.hlsl
// 頂点バッファなし：SkinningCS の出力（位置・法線・接線はすべて
// そこでスキニング済み）を読み、標準のモデル VS に
// 通す。
//
// skinnedVerts は ModelCommon の albedoTexture と同じ t0 に置く。
// VS はテクスチャを参照しないので衝突しない。
// ============================================================
#include "../Common/ModelCommon.hlsli"

struct SkinnedVertexOut
{
    float3 position;
    float _pad0;
    float3 normal;
    float _pad1;
    float3 tangent;
    float _pad2;
    float2 uv;
    float2 _pad3;
};

StructuredBuffer<SkinnedVertexOut> skinnedVerts : register(t0);

VS_OUTPUT main(uint vid : SV_VertexID)
{
    SkinnedVertexOut v = skinnedVerts[vid];

    VS_INPUT i;
    i.Position = v.position;
    i.Normal = v.normal;
    i.Tangent = v.tangent;
    i.UV = v.uv;
    i.Color = float4(1.0, 1.0, 1.0, 1.0);
    return ModelVS(i);
}