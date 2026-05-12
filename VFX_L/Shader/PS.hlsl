Texture2D diffuseTexture : register(t0);
SamplerState samplerState : register(s0);

struct DirectionalLight
{
    float3 direction;
    float padding1;
    float3 color;
    float intensity;
};

cbuffer LightBuffer : register(b0)
{
    DirectionalLight dirLight;
    float3 ambientColor;
    float padding;
    float3 cameraPosition;
    float padding2;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD1;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 texColor = diffuseTexture.Sample(samplerState, input.UV);
    
    float3 normal = normalize(input.Normal);
    
    // Lambert
    float3 lightDir = normalize(-dirLight.direction);
    float NdotL = max(dot(normal, lightDir), 0.0f);
    float3 diffuse = dirLight.color * dirLight.intensity * NdotL;
    
    float3 ambient = ambientColor;
    
    float3 finalColor = (ambient + diffuse) * texColor.rgb * input.Color.rgb;
    
    return float4(finalColor, texColor.a * input.Color.a);
}