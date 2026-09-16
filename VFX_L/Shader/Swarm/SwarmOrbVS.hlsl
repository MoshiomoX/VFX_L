// ============================================================
// SwarmOrbVS.hlsl
// Instanced exp orb mesh. Same skeleton as SwarmEnemyVS, no
// rotation (sphere). Attracted orbs tint toward cyan.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmOrb> orbs : register(t0);
Buffer<uint> orbStates : register(t1);

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;

    if (orbStates[input.InstanceID] == SWARM_DEAD)
    {
        o.Position = float4(0, 0, -1, 1);
        return o;
    }

    SwarmOrb b = orbs[input.InstanceID];

    float3 worldPos = input.Position + b.position;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = input.Normal;
    o.Tangent = input.Tangent;
    o.UV = input.UV;

    // _pad = pull speed. > 0 means attracted -> shift toward cyan
    float t = saturate(b._pad * 0.1);
    o.Color = lerp(input.Color, float4(0.4, 1.0, 1.0, 1.0), t);
    return o;
}