Texture2D particleTexture : register(t0);
SamplerState samplerState : register(s0);

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 texColor = particleTexture.Sample(samplerState, input.UV);
    return texColor * input.Color;
}