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
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD1;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    
    float4 worldPos = mul(float4(input.Position, 1.0f), World);
    output.WorldPos = worldPos.xyz;
    
    output.Position = mul(worldPos, View);
    output.Position = mul(output.Position, Projection);
    
    output.Normal = normalize(mul(input.Normal, (float3x3) World));
    output.UV = input.UV;
    output.Color = input.Color;
    
    return output;
}