// ============================================================
// SkinnedVS.hlsl
// No vertex buffer: reads the SkinningCS output and feeds it
// through the standard model VS. Tangent is not skinned yet, so
// a constant is written (Lambert PS does not read it).
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
    float2 uv;
    float2 _pad2;
};

StructuredBuffer<SkinnedVertexOut> skinnedVerts : register(t0);

VS_OUTPUT main(uint vid : SV_VertexID)
{
    SkinnedVertexOut v = skinnedVerts[vid];

    VS_INPUT i;
    i.Position = v.position;
    i.Normal = v.normal;
    i.Tangent = float3(1.0, 0.0, 0.0);
    i.UV = v.uv;
    i.Color = float4(1.0, 1.0, 1.0, 1.0);
    return ModelVS(i);
}