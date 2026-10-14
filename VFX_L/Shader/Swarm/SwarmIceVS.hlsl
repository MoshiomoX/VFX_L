// ============================================================
// SwarmIceVS.hlsl
// Ice crystals around a frozen enemy (Ice Lance, 2026-10-08).
// Two instances per enemy slot (DrawIndexedInstanced over the whole
// pool x 2); slots that are dead or not frozen (enemySlow.z <= 0) are
// collapsed. The mesh is a VFX model (Assets/VFX/Mesh/bingci_02.FBX, a
// cluster of upright spikes) drawn with the enemy pixel shader and the
// default white texture, alpha blended (premultiplied: a glowing,
// see-through ice).
//
// Placement: the mesh's bottom centre (g_MeshAnchor) sits at the feet,
// one cluster in front, one behind, turned by a per-slot hash. It shoots
// up in the first ICE_GROW seconds of the freeze (enemySlow.w = length
// of the freeze, so w - z = time frozen) and shrinks in the last
// ICE_MELT seconds.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<float4> enemySlow : register(t4);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);

// C++ mirror: SwarmSystem::PropMeshCB (same layout as SwarmShieldVS)
cbuffer SwarmPropMeshCB : register(b4)
{
    float3 g_MeshAnchor; // model-space bottom centre of the cluster
    float g_MeshScale;   // model units -> meters (a normal body's cluster)
    float3 g_HoldOffset; // x = distance of each cluster from the body centre, y = sink below the feet
    float g_MeshYaw;     // unused
    float4 g_IceColor;   // linear, may go above 1 (bloom)
    float g_BobAmount;   // unused
    float g_IceAlpha;
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

static const float ICE_GROW = 0.12;
static const float ICE_MELT = 0.25;

float3 RotateY(float3 p, float s, float c)
{
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    uint slot = input.InstanceID >> 1;
    uint variant = input.InstanceID & 1u;
    float4 st = (slot < g_MaxEnemies) ? enemySlow[slot] : float4(0, 0, 0, 0);
    if (slot >= g_MaxEnemies || enemyStates[slot] == SWARM_DEAD || st.z <= 0.0)
    {
        o.Position = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmEnemy e = enemies[slot];
    float k = SwarmKindScale(enemyExtra[slot].kind);
    float elapsed = max(st.w - st.z, 0.0);
    float size = k * smoothstep(0.0, ICE_GROW, elapsed) * lerp(0.5, 1.0, saturate(st.z / ICE_MELT));
    size *= (variant == 0u) ? 1.0 : 0.8;

    // per-slot turn, the second cluster on the opposite side
    float a = frac((float) slot * 0.6180339) * 6.2831853 + (float) variant * 3.1415927;
    float as, ac;
    sincos(a, as, ac);
    float3 local = RotateY((input.Position - g_MeshAnchor) * g_MeshScale * size, as, ac);
    float3 normal = RotateY(input.Normal, as, ac);

    float footY = e.position.y - (g_EnemyRadius + g_EnemyCapsuleHalf);
    float3 worldPos = local + float3(e.position.x + as * g_HoldOffset.x * k,
                                     footY - g_HoldOffset.y,
                                     e.position.z + ac * g_HoldOffset.x * k);
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normal;
    o.Tangent = RotateY(input.Tangent, as, ac);
    o.UV = input.UV;
    o.Color = float4(g_IceColor.rgb, g_IceAlpha);
    return o;
}
