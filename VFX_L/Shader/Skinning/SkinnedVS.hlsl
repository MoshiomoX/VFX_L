#pragma pack_matrix(row_major)

struct SkinnedVertexOut
{
    float3 position;
    float _pad0;
    float3 normal;
    float _pad1;
    float2 uv;
    float2 _pad2;
};

cbuffer RenderCB : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
};

StructuredBuffer<SkinnedVertexOut> skinnedVerts : register(t0);

struct VSOut
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)   // 頂点バッファ無し。skinned bufferから読む
{
    SkinnedVertexOut v = skinnedVerts[vid];

    float4 wp = mul(float4(v.position, 1.0), World);
    VSOut o;
    o.worldPos = wp.xyz;
    o.position = mul(mul(wp, View), Projection);
    o.normal = normalize(mul(v.normal, (float3x3) World));
    o.uv = v.uv;
    return o;
}