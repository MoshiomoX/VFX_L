// ============================================================
// SkinnedVS.hlsl
// No vertex buffer: reads the SkinningCS output (position, normal
// and tangent all skinned there) and feeds it through the standard
// model VS.
//
// skinnedVerts sits on t0 next to ModelCommon's albedoTexture.
// A VS never references the texture, so no conflict.
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