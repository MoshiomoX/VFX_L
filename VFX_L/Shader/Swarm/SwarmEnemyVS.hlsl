// ============================================================
// SwarmEnemyVS.hlsl
// Instanced mob mesh. Position / yaw come from the enemy buffer,
// dead slots are collapsed behind the near plane.
//
// Uses ModelCommon's MVPBuffer on b0 (World is identity; the C++
// side writes it). SwarmCommon's cbuffers are moved off b0.
// enemies / enemyStates share t0 / t1 with ModelCommon's textures,
// which a VS never references.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

// yaw = atan2(v.x, v.z): forward (0,0,1) -> (sin, 0, cos)
float3 RotateY(float3 p, float s, float c)
{
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

VS_OUTPUT main(VS_INPUT_INST input)
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
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = RotateY(input.Normal, s, c);
    o.Tangent = RotateY(input.Tangent, s, c);
    o.UV = input.UV;
    o.Color = input.Color;
    return o;
}