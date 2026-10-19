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
// kind's body size (SwarmKindScale) and rolls about the axis
// perpendicular to its velocity: the angle comes from the distance it
// walked (animTime x speed / radius, animTime advances while walking).
// Hit flash and ice tint like the body. Frozen = stops rolling.
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
    float g_BobAmount;   // roll rate multiplier (1 = exact rolling)
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

// Rodrigues rotation of p about the unit axis by angle a
float3 RotateAxis(float3 p, float3 axis, float a)
{
    float s, c;
    sincos(a, s, c);
    return p * c + cross(axis, p) * s + axis * dot(axis, p) * (1.0 - c);
}

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

    // rolling: axis = up x travel direction, angle = distance / radius
    float2 v = e.velocity.xz;
    float speed = length(v);
    float3 local = input.Position * radius;
    float3 normal = input.Normal;
    if (speed > 0.05)
    {
        float3 dir = float3(v.x / speed, 0.0, v.y / speed);
        float3 axis = normalize(cross(float3(0, 1, 0), dir));
        float angle = e.animTime * speed / max(radius, 1e-3) * g_BobAmount + (float) slot * 1.7;
        local = RotateAxis(local, axis, angle);
        normal = RotateAxis(normal, axis, angle);
    }
    else
    {
        // at rest (or frozen): keep a fixed random pose per slot so it does not snap
        float3 axis = normalize(float3(0.3, 1.0, 0.2));
        local = RotateAxis(local, axis, (float) slot * 1.7);
        normal = RotateAxis(normal, axis, (float) slot * 1.7);
    }

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
