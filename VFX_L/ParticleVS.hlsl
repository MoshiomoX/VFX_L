cbuffer ParticleCB : register(b0)
{
    row_major matrix View;
    row_major matrix Projection;
    float3 CameraRight;
    float padding1;
    float3 CameraUp;
    float padding2;
};

struct VS_INPUT
{
    float3 Position : POSITION;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    float Size : SIZE;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 worldPos = float4(input.Position, 1.0f);
    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);

    output.UV = input.UV;
    output.Color = input.Color;

    return output;
}