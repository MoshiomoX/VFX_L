// ============================================================
// SwarmSpikeBallVS.hlsl
// The spike ball (SWARM_KIND_SPIKEBALL, 2026-10-17): a desert enemy
// that is a rolling ball of spikes instead of a blocky body. One
// instance per spike ball (the spike ball draw list, count by
// DrawIndexedInstancedIndirect). The mesh is built in code
// (PrimitiveBuilder::CreateSpikeBall, radius 1, colours in the vertices)
// and drawn with the enemy pixel shader and the default white texture,
// so the colour is vertex colour x g_ShieldTint.
//
// The ball sits on the ground (centre = feet + radius), scaled by the
// kind's body size (SwarmKindScale) and turned by its orientation in
// enemyRoll (2026-10-25): SwarmEnemyMoveCS chains every step's roll
// (distance moved / radius about up x travel) onto it, so speeding up,
// slowing down and turning never snap the pose (it used to be animTime x
// speed / radius about the current velocity). A ball that has never rolled
// keeps a fixed pose per slot. Hit flash and ice tint like the body.
// Frozen = stops rolling.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> spikeBallList : register(t2);
StructuredBuffer<float4> enemyRoll : register(t3);   // orientation (quaternion) per slot, SwarmEnemyMoveCS
StructuredBuffer<float4> enemySlow : register(t4);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);

// C++ mirror: SwarmSystem::PropMeshCB (same block as SwarmShieldVS)
cbuffer SwarmPropMeshCB : register(b4)
{
    float3 g_MeshAnchor; // unused (the mesh is centred on its origin)
    float g_MeshScale;   // mesh radius 1 -> metres (the body radius at scale 1)
    float3 g_HoldOffset; // y = centre height above the feet at scale 1
    float g_MeshYaw;     // unused
    float4 g_ShieldTint; // multiplies the vertex colour
    float g_BobAmount;   // unused (the roll rate goes to MoveCS as BomberCB's g_SpikeBallRollPerMeter, 2026-10-25)
    float g_PropAlpha;   // unused
    float2 _propPad;
};

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

static const float3 kIceTint = float3(0.75, 1.05, 1.7); // same as SwarmEnemyVS

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    uint slot = spikeBallList[input.InstanceID];
    SwarmEnemyExtra ex = enemyExtra[slot];
    if (enemyStates[slot] == SWARM_DEAD || ex.kind != SWARM_KIND_SPIKEBALL)
    {
        o.Position = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmEnemy e = enemies[slot];
    bool frozen = enemySlow[slot].z > 0.0;
    float k = SwarmKindScale(ex.kind);
    float radius = g_MeshScale * k;

    // rolling: the orientation MoveCS has built up from the distance it really moved (see the header)
    float4 roll = SwarmSpikeBallRoll(enemyRoll[slot], slot);
    float3 local = SwarmQuatRotate(roll, input.Position * radius);
    float3 normal = SwarmQuatRotate(roll, input.Normal);

    float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
    float3 worldPos = e.position + float3(0.0, footY + g_HoldOffset.y * k, 0.0) + local;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normal;
    o.Tangent = float3(1, 0, 0);
    o.UV = input.UV;
    o.Color = input.Color * g_ShieldTint;
    if (e.animIndex == 2u)
    {
        float t = saturate(e.animTime / max(g_HitStun, 1e-4));
        o.Color.rgb *= lerp(g_HitFlash, 1.0, t);
    }
    if (frozen)
        o.Color.rgb *= lerp(float3(1, 1, 1), kIceTint, saturate(g_FreezeTint));
    return o;
}
