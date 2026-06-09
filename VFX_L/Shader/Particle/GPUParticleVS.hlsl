#include "Common/ParticleCommon.hlsli"

cbuffer ParticleRenderCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
    float3 g_CameraPosition;
    float _pad0;
};

StructuredBuffer<GPUParticle> particles : register(t0);
StructuredBuffer<uint> aliveList : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation int atlasRows : TEXCOORD1;
    nointerpolation int atlasCols : TEXCOORD2;
    nointerpolation int uvFrame : TEXCOORD3;
};

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output = (VSOutput) 0;

    uint particleIndex = aliveList[instanceID]; // ← 改成用 instanceID
    GPUParticle p = particles[particleIndex];

    if (p.isAlive < 0.5)
    {
        output.position = float4(0, 0, -1, 1);
        return output;
    }

    static const float2 corners[6] =
    {
        float2(-1, 1), float2(1, 1), float2(-1, -1),
        float2(-1, -1), float2(1, 1), float2(1, -1),
    };

    static const float2 uvs[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1),
    };

    uint cornerIndex = vertexID; // ← 直接用 vertexID（0~5）
    float2 corner = corners[cornerIndex];
    output.uv = uvs[cornerIndex];

    output.atlasRows = p.atlasRows;
    output.atlasCols = p.atlasCols;
    output.uvFrame = p.uvFrame;

    float3 camRight = float3(g_View[0][0], g_View[1][0], g_View[2][0]);
    float3 camUp = float3(g_View[0][1], g_View[1][1], g_View[2][1]);

    float cosR = cos(p.rotation);
    float sinR = sin(p.rotation);
    float2 rotated;
    rotated.x = corner.x * cosR - corner.y * sinR;
    rotated.y = corner.x * sinR + corner.y * cosR;

    float halfSize = p.size * 0.5;
    float3 worldPos = p.position
                    + camRight * rotated.x * halfSize
                    + camUp * rotated.y * halfSize;

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    output.position = mul(viewPos, g_Projection);
    output.color = p.color;

    return output;
}