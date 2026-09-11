// ============================================================
// SwarmEnemyVS.hlsl
// Instanced mesh for GPU enemies. One instance per pool slot; the
// VS reads position / yaw straight from the enemy buffer, so no
// per-instance data ever touches the CPU.
//
// Dead slots are collapsed behind the near plane (same trick as the
// debug VS). DrawIndexedInstanced with kMaxEnemies instances; the
// wasted vertex work on dead slots is the price of not sorting.
//
// Output layout is identical to VS.hlsl so the default PS.hlsl
// (Lambert + vertex color) can be reused as-is.
// ============================================================

// SwarmCommon owns b0 by default; move it out of the way (unused here)
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmEnemyRenderCB : register(b0)
{
    row_major matrix View;
    row_major matrix Projection;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);

struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD1;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

// rotate about Y by yaw. matches XMMatrixRotationY applied to a
// row vector: x' = x*c + z*s, z' = -x*s + z*c
float3 RotateY(float3 v, float s, float c)
{
    return float3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
}

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;

    if (enemyStates[input.InstanceID] == SWARM_DEAD)
    {
        o.Position = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmEnemy e = enemies[input.InstanceID];

    float s, c;
    sincos(e.yaw, s, c);

    float3 worldPos = RotateY(input.Position, s, c) + e.position;
    o.WorldPos = worldPos;

    float4 viewPos = mul(float4(worldPos, 1.0), View);
    o.Position = mul(viewPos, Projection);

    o.Normal = normalize(RotateY(input.Normal, s, c));
    o.UV = input.UV;
    o.Color = input.Color;
    return o;
}