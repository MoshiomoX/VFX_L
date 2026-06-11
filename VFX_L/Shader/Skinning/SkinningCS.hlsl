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

    // 4ボーンの行列を重み混合
    float4x4 m =
          bonePalette[v.boneIndices.x] * v.boneWeights.x +
          bonePalette[v.boneIndices.y] * v.boneWeights.y +
          bonePalette[v.boneIndices.z] * v.boneWeights.z +
          bonePalette[v.boneIndices.w] * v.boneWeights.w;

    SkinnedVertexOut o;
    o.position = mul(float4(v.position, 1.0), m).xyz; // 行ベクトル v*M
    o.normal = normalize(mul(v.normal, (float3x3) m));
    o.uv = v.uv;
    o._pad0 = 0;
    o._pad1 = 0;
    o._pad2 = float2(0, 0);

    outVerts[i] = o;
}