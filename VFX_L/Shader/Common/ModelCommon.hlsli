// ============================================================
// ModelCommon.hlsli
// Shared interface for every model VS / PS:
//   - MVP constant buffer (VS b0, row_major, matches C++ MVPBuffer)
//   - vertex input / VS-to-PS layout
//   - texture slot convention
//   - the standard model VS body
//
// Register macros can be overridden before #include when a
// shader needs the slot for something else (same pattern as
// SWARM_FRAME_CB_REG).
// ============================================================
#ifndef MODEL_COMMON_HLSLI
#define MODEL_COMMON_HLSLI

#ifndef MODEL_MVP_CB_REG
#define MODEL_MVP_CB_REG b0
#endif

cbuffer MVPBuffer : register(MODEL_MVP_CB_REG)
{
    row_major matrix World;
    row_major matrix View;
    row_major matrix Projection;
};

// ---- vertex layout (PrimitiveBuilder / Model loader) ----
struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

// ---- VS -> PS. One layout for every model shader. ----
// Shaders that do not have a tangent write 0; PS that do not
// need it simply ignore it.
struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD1;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};
typedef VS_OUTPUT PS_INPUT;

// ---- texture slots (Material binds by slot) ----
//   t0 albedo / t1 normal / t2 metallic / t3 roughness / t4 AO
Texture2D albedoTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicTexture : register(t2);
Texture2D roughnessTexture : register(t3);
Texture2D aoTexture : register(t4);
SamplerState samplerState : register(s0);

// ---- standard model VS ----
VS_OUTPUT ModelVS(VS_INPUT input)
{
    VS_OUTPUT o;
    float4 worldPos = mul(float4(input.Position, 1.0), World);
    o.WorldPos = worldPos.xyz;
    o.Position = mul(mul(worldPos, View), Projection);
    o.Normal = normalize(mul(input.Normal, (float3x3) World));
    o.Tangent = normalize(mul(input.Tangent, (float3x3) World));
    o.UV = input.UV;
    o.Color = input.Color;
    return o;
}

#endif