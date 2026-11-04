// ============================================================
// SkinningCS.hlsl
// バインド時の頂点 x ボーンパレット -> スキニング済みの頂点バッファ。
// 位置 / 法線 / 接線はどれも同じ 4 本のボーンを通す。
// ============================================================
#pragma pack_matrix(row_major)

// C++ の SkinnedVertex と揃える（96 バイト）
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

// C++ の SkinnedVertexOut と揃える（48 バイト）
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
    // 接線の無いメッシュ（ゼロベクトル）は NaN にせず 0 のままにする。そうすれば PerturbNormal は法線をそのまま使う
    o.tangent = (dot(t, t) > 1e-12) ? normalize(t) : float3(0.0, 0.0, 0.0);
    o.uv = v.uv;
    o._pad0 = 0;
    o._pad1 = 0;
    o._pad2 = 0;
    o._pad3 = float2(0, 0);
    outVerts[i] = o;
}