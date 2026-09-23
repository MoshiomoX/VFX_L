// ============================================================
// SkinningCS.hlsl
// bind vertex x bone palette -> skinned vertex buffer.
// Position / normal / tangent all go through the same 4 bones.
// ============================================================
#pragma pack_matrix(row_major)

// matches C++ SkinnedVertex (96 bytes)
struct SkinnedVertex
{
    float3 position;
    float _pad0;
    float3 normal;
    float _pad1;
    float3 tangent;
    float _pad3;
    float2 uv;
    float2 _pad2;
    uint4 boneIndices;
    float4 boneWeights;
};

// matches C++ SkinnedVertexOut (48 bytes)
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

cbuffer SkinCB : register(b0)
{
    uint g_VertexCount;
    uint3 _pad;
};

StructuredBuffer<SkinnedVertex> bindVerts : register(t0);
StructuredBuffer<float4x4> bonePalette : register(t1);
RWStructuredBuffer<SkinnedVertexOut> outVerts : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_VertexCount)
        return;

    SkinnedVertex v = bindVerts[i];

    float4x4 m0 = transpose(bonePalette[v.boneIndices.x]);
    float4x4 m1 = transpose(bonePalette[v.boneIndices.y]);
    float4x4 m2 = transpose(bonePalette[v.boneIndices.z]);
    float4x4 m3 = transpose(bonePalette[v.boneIndices.w]);
    float4 w = v.boneWeights;

    float4 pos = float4(v.position, 1.0);
    float3 p = mul(pos, m0).xyz * w.x + mul(pos, m1).xyz * w.y
             + mul(pos, m2).xyz * w.z + mul(pos, m3).xyz * w.w;

    float3 n = mul(v.normal, (float3x3) m0) * w.x + mul(v.normal, (float3x3) m1) * w.y
             + mul(v.normal, (float3x3) m2) * w.z + mul(v.normal, (float3x3) m3) * w.w;

    float3 t = mul(v.tangent, (float3x3) m0) * w.x + mul(v.tangent, (float3x3) m1) * w.y
             + mul(v.tangent, (float3x3) m2) * w.z + mul(v.tangent, (float3x3) m3) * w.w;

    SkinnedVertexOut o;
    o.position = p;
    o.normal = normalize(n);
    o.tangent = normalize(t);
    o.uv = v.uv;
    o._pad0 = 0;
    o._pad1 = 0;
    o._pad2 = 0;
    o._pad3 = float2(0, 0);
    outVerts[i] = o;
}