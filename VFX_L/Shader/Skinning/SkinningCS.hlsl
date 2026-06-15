// SkinningCS.hlsl
// GPU skinning — bind頂点 × ボーン行列パレット → skinning結果をbufferへ書く
#pragma pack_matrix(row_major)  
// C++ SkinnedVertex と厳密一致（80byte）
struct SkinnedVertex
{
    float3 position;
    float _pad0;
    float3 normal;
    float _pad1;
    float2 uv;
    float2 _pad2;
    uint4 boneIndices;
    float4 boneWeights;
};

// C++ SkinnedVertexOut と厳密一致（48byte）
struct SkinnedVertexOut
{
    float3 position;
    float _pad0;
    float3 normal;
    float _pad1;
    float2 uv;
    float2 _pad2;
};

cbuffer SkinCB : register(b0)
{
    uint g_VertexCount;
    uint3 _pad;
};

StructuredBuffer<SkinnedVertex> bindVerts : register(t0); // 入力: bind pose
StructuredBuffer<float4x4> bonePalette : register(t1); // 入力: ボーン最終行列
RWStructuredBuffer<SkinnedVertexOut> outVerts : register(u0); // 出力

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_VertexCount)
        return;

    SkinnedVertex v = bindVerts[i];

    // ★各ボーンで頂点を変換してから重み付け加算（matrix*scalarの型曖昧さを回避）
    float4 pos = float4(v.position, 1.0);
    float3 nrm = v.normal;

    float4 skinnedPos = float4(0, 0, 0, 0);
    float3 skinnedNrm = float3(0, 0, 0);

    float4x4 m0 = transpose(bonePalette[v.boneIndices.x]);
    float4x4 m1 = transpose(bonePalette[v.boneIndices.y]);
    float4x4 m2 = transpose(bonePalette[v.boneIndices.z]);
    float4x4 m3 = transpose(bonePalette[v.boneIndices.w]);

    skinnedPos += mul(pos, m0) * v.boneWeights.x;
    skinnedPos += mul(pos, m1) * v.boneWeights.y;
    skinnedPos += mul(pos, m2) * v.boneWeights.z;
    skinnedPos += mul(pos, m3) * v.boneWeights.w;

    skinnedNrm += mul(nrm, (float3x3) m0) * v.boneWeights.x;
    skinnedNrm += mul(nrm, (float3x3) m1) * v.boneWeights.y;
    skinnedNrm += mul(nrm, (float3x3) m2) * v.boneWeights.z;
    skinnedNrm += mul(nrm, (float3x3) m3) * v.boneWeights.w;

    SkinnedVertexOut o;
    o.position = skinnedPos.xyz;
    o.normal = normalize(skinnedNrm);
    o.uv = v.uv;
    o._pad0 = 0;
    o._pad1 = 0;
    o._pad2 = float2(0, 0);

    outVerts[i] = o;
}