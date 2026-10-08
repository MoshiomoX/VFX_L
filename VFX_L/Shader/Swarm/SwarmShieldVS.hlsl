// ============================================================
// SwarmShieldVS.hlsl
// The tower shield the shield bearer holds in front of its body
// (2026-10-08). One instance per shield bearer (the shield draw list,
// count by DrawIndexedInstancedIndirect), the mesh is a VFX model
// (Assets/VFX/Mesh/dun01.FBX) drawn with the enemy pixel shader and the
// default white texture, so the colour is g_ShieldTint.
//
// The mesh is moved so its centre (g_MeshAnchor) sits at g_HoldOffset in
// the enemy's frame (x right, y above the feet, z forward), scaled by
// g_MeshScale and turned by g_MeshYaw. It bobs a little while walking,
// grows with the body (SwarmKindScale, anchored at the feet) and takes
// the same hit flash / ice tint as the body.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> shieldList : register(t2);
StructuredBuffer<float4> enemySlow : register(t4);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);

// C++ mirror: SwarmSystem::PropMeshCB
cbuffer SwarmPropMeshCB : register(b4)
{
    float3 g_MeshAnchor; // model-space point placed at g_HoldOffset
    float g_MeshScale;   // model units -> meters
    float3 g_HoldOffset; // enemy frame, meters (y from the feet)
    float g_MeshYaw;     // radians, turns the mesh before it is placed
    float4 g_ShieldTint; // vertex colour (linear; the PS multiplies the white texture by it)
    float g_BobAmount;   // meters of walk bob
    float g_PropAlpha;   // unused here (SwarmIceVS)
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

float3 RotateY(float3 p, float s, float c)
{
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    uint slot = shieldList[input.InstanceID];
    SwarmEnemyExtra ex = enemyExtra[slot];
    if (enemyStates[slot] == SWARM_DEAD || ex.kind != SWARM_KIND_SHIELD)
    {
        o.Position = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmEnemy e = enemies[slot];
    bool frozen = enemySlow[slot].z > 0.0;
    bool moving = dot(e.velocity.xz, e.velocity.xz) > 0.01 && !frozen && e.animIndex != 2u;

    float ms, mc;
    sincos(g_MeshYaw, ms, mc);
    float3 local = RotateY((input.Position - g_MeshAnchor) * g_MeshScale, ms, mc);
    float3 normal = RotateY(input.Normal, ms, mc);
    float3 tangent = RotateY(input.Tangent, ms, mc);

    float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
    float bob = moving ? abs(sin(e.animTime * 9.0 + (float) slot * 0.37)) * g_BobAmount : 0.0;
    local += g_HoldOffset;
    local.y += footY + bob;

    // grows with the body, anchored at the feet (same as the body in SwarmEnemyVS)
    float k = SwarmKindScale(ex.kind);
    local.xz *= k;
    local.y = footY + (local.y - footY) * k;

    float s, c;
    sincos(e.yaw, s, c);
    float3 worldPos = RotateY(local, s, c) + e.position;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = RotateY(normal, s, c);
    o.Tangent = RotateY(tangent, s, c);
    o.UV = input.UV;
    o.Color = g_ShieldTint;
    if (e.animIndex == 2u)
    {
        float t = saturate(e.animTime / max(g_HitStun, 1e-4));
        o.Color.rgb *= lerp(g_HitFlash, 1.0, t);
    }
    if (frozen)
        o.Color.rgb *= lerp(float3(1, 1, 1), kIceTint, saturate(g_FreezeTint));
    return o;
}
