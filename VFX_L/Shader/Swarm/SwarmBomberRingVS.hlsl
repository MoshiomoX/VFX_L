// ============================================================
// SwarmBomberRingVS.hlsl
// Warning ring on the ground under every LIT bomber: the rim marks
// g_BomberBlastRadius, a disc grows from the centre with the fuse and
// touches the rim when it blows up. One ground quad per instance,
// 6 vertices from SV_VertexID, no vertex buffer.
//
// Instances come from the bomber list (SwarmEnemyCompactCS), drawn
// with DrawInstancedIndirect. Bombers that are not lit (or already
// dead) collapse behind the near plane, so the ring disappears the
// moment a bomber is killed during its fuse.
//
// The quad lies flat at the bomber's foot height (position.y - groundY
// = terrain height, see SwarmEnemyMoveCS) plus g_RingLift. On a ramp
// part of it may sink under the slope.
//
// C++ mirror of the cbuffer: SwarmSystem::BomberRingCB
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b4
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmBomberRingCB : register(b0)
{
    row_major float4x4 g_RingView;
    row_major float4x4 g_RingProj;
    float4 g_RingFill;  // growing disc (straight alpha)
    float4 g_RingEdge;  // rim
    float4 g_RingBack;  // inside the rim, not filled yet
    float g_RingEdgeWidth; // rim thickness, meters
    float g_RingLift;      // meters above the ground
    float2 _ringPad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> bomberList : register(t2);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3);

struct RingOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;       // -1..1 across the blast radius
    float progress : TEXCOORD1;  // fuse 0..1 = radius of the filled disc in uv units
    float edgeWidth : TEXCOORD2; // rim thickness in uv units
};

static const float2 kCorner[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1)
};

RingOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    RingOut o = (RingOut) 0;

    uint slot = bomberList[iid];
    SwarmEnemyExtra extra = enemyExtra[slot];
    bool lit = (extra.kind == SWARM_KIND_BOMBER) && (extra.fuse > 0.0);
    if (enemyStates[slot] == SWARM_DEAD || !lit)
    {
        o.pos = float4(0, 0, -1, 1); // clipped
        return o;
    }

    float radius = max(g_BomberBlastRadius, 0.01);
    float3 p = enemies[slot].position;
    float2 c = kCorner[vid];
    float3 world = float3(p.x + c.x * radius,
                          p.y - g_GroundY + g_RingLift,
                          p.z + c.y * radius);

    o.pos = mul(mul(float4(world, 1.0), g_RingView), g_RingProj);
    o.uv = c;
    o.progress = saturate(extra.fuse / max(g_BomberFuseTime, 1e-3));
    o.edgeWidth = g_RingEdgeWidth / radius;
    return o;
}
