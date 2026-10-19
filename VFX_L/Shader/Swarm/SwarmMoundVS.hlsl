// ============================================================
// SwarmMoundVS.hlsl
// The sand mound of a burrower (SWARM_KIND_BURROWER, 2026-10-17): while
// the enemy travels underground its blocky body is sunk below the ground
// (SwarmEnemyVS) and this low dome moves along on the surface instead.
// One instance per burrower (the burrower draw list, count by
// DrawIndexedInstancedIndirect); the mesh is a unit sphere built in code,
// squashed to (radius, height, radius) x body size and centred on the
// feet, so the lower half is under the terrain and only a dome shows.
// Rises / flattens with the enemy's surfacing (fuse), bobs while moving.
// Drawn with the enemy pixel shader and the default white texture, so the
// colour is vertex colour x g_ShieldTint.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> burrowerList : register(t2);
StructuredBuffer<float4> enemySlow : register(t4);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);

// C++ mirror: SwarmSystem::PropMeshCB (same block as SwarmShieldVS)
cbuffer SwarmPropMeshCB : register(b4)
{
    float3 g_MeshAnchor; // unused
    float g_MeshScale;   // mound radius (m) at body size 1
    float3 g_HoldOffset; // y = mound height (m) at body size 1
    float g_MeshYaw;     // unused
    float4 g_ShieldTint; // multiplies the vertex colour
    float g_BobAmount;   // bob while moving (m)
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

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    uint slot = burrowerList[input.InstanceID];
    SwarmEnemyExtra ex = enemyExtra[slot];
    // how far under the ground the body is: 1 = fully buried (mound full), 0 = out (no mound)
    float down = (ex.fuse > 0.0) ? 1.0 - saturate((g_BurrowSurfaceTime - ex.fuse) / SWARM_BURROW_RISE_TIME)
                                 : saturate(-ex.fuse / SWARM_BURROW_RISE_TIME);
    if (enemyStates[slot] == SWARM_DEAD || ex.kind != SWARM_KIND_BURROWER || down < 0.02)
    {
        o.Position = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmEnemy e = enemies[slot];
    float k = SwarmKindScale(ex.kind);
    float r = g_MeshScale * k;
    float h = g_HoldOffset.y * k * down;
    bool moving = dot(e.velocity.xz, e.velocity.xz) > 0.01;
    float bob = moving ? abs(sin(e.animTime * 7.0 + (float) slot * 0.37)) * g_BobAmount : 0.0;

    float3 local = input.Position * float3(r, h, r);
    // normal of the squashed sphere: scale by the inverse
    float3 normal = normalize(input.Normal * float3(1.0 / max(r, 1e-3), 1.0 / max(h, 1e-3), 1.0 / max(r, 1e-3)));

    float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
    float3 worldPos = e.position + float3(0.0, footY + bob - 0.05, 0.0) + local;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normal;
    o.Tangent = float3(1, 0, 0);
    o.UV = input.UV;
    o.Color = input.Color * g_ShieldTint;
    return o;
}
