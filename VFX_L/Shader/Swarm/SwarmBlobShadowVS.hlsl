// ============================================================
// SwarmBlobShadowVS.hlsl
// Round shadow on the ground under every live mob. Mobs are not in
// the sun shadow map (ShadowMap), this stands in for them. One ground
// quad per instance, 6 vertices from SV_VertexID, no vertex buffer.
//
// Instances come from the same alive list as the mob mesh and the HP
// bars (SwarmEnemyCompactCS), drawn with DrawInstancedIndirect (the HP
// bar args: 6 vertices x alive count).
//
// Each corner sits on the terrain height field at its own xz (the
// quad follows the long terrace slopes instead of cutting into them),
// kept within g_BlobClamp of the foot height so a mob at a cliff edge
// does not stretch its quad down the cliff.
//
// C++ mirror of the cbuffer: SwarmSystem::BlobShadowCB
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmBlobShadowCB : register(b0)
{
    row_major float4x4 g_BlobView;
    row_major float4x4 g_BlobProj;
    float g_BlobRadius;    // meters
    float g_BlobStrength;  // darkness at the centre, 0..1
    float g_BlobLift;      // meters above the ground (z-fighting)
    float g_BlobSoftness;  // 0..1, how much of the radius fades out
    float g_BlobClamp;     // max corner height difference from the foot, meters
    float3 _blobPad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> aliveList : register(t2);
StructuredBuffer<float> heights : register(t3);

struct BlobOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0; // -1..1 across the blob
};

static const float2 kCorner[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1)
};

BlobOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    BlobOut o = (BlobOut) 0;
    o.pos = float4(0, 0, -1, 1); // clipped unless alive

    uint slot = aliveList[iid];
    if (enemyStates[slot] != SWARM_DEAD)
    {
        float3 p = enemies[slot].position;
        float footY = p.y - g_GroundY;
        float2 c = kCorner[vid];
        float2 xz = p.xz + c * g_BlobRadius;
        float y = clamp(SwarmTerrainHeight(heights, xz), footY - g_BlobClamp, footY + g_BlobClamp);
        float3 world = float3(xz.x, y + g_BlobLift, xz.y);

        o.pos = mul(mul(float4(world, 1.0), g_BlobView), g_BlobProj);
        o.uv = c;
    }
    return o;
}
