// ============================================================
// SkyVS.hlsl
// スカイボックス用 頂点シェーダー
// 球をそのまま投影（ライティング無し）
// ============================================================
cbuffer MVPBuffer : register(b0)
{
    row_major matrix World;
    row_major matrix View;
    row_major matrix Projection;
};

struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 worldPos = mul(float4(input.Position, 1.0f), World);
    output.Position = mul(worldPos, View);
    output.Position = mul(output.Position, Projection);

    output.UV = input.UV;
    return output;
}