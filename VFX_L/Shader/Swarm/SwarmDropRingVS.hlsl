// ============================================================
// SwarmDropRingVS.hlsl
// Warning ring on the ground where every falling meteor (DROP motion)
// will land. Same look as the bomber ring: the rim marks the radius of
// the area the meteor leaves (its motion's hitArea), a disc grows from
// the centre as it falls (pathT) and touches the rim on impact.
// The pixel shader is SwarmBomberRingPS (same b0 layout); only the
// colours differ (SwarmSystem::dropRing).
//
// One instance per projectile slot (DrawInstanced, no list): slots that
// are dead or not DROP collapse behind the near plane. 6 vertices of a
// ground quad from SV_VertexID, no vertex buffer.
//
// The impact point p3 is the enemy's position at spawn time, which sits
// g_GroundY above the terrain (see SwarmEnemyMoveCS), so the quad goes
// at p3.y - g_GroundY + g_RingLift.
//
// C++ mirror of the cbuffer: SwarmSystem::BomberRingCB
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
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

StructuredBuffer<SwarmProjectile> projectiles : register(t0);
Buffer<uint> projStates : register(t1);
StructuredBuffer<SwarmProjPath> paths : register(t2);
StructuredBuffer<SwarmMotion> motions : register(t3);
StructuredBuffer<SwarmAreaDef> areaDefs : register(t4);

struct RingOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;       // -1..1 across the area radius
    float progress : TEXCOORD1;  // fall 0..1 = radius of the filled disc in uv units
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
    o.pos = float4(0, 0, -1, 1); // clipped unless this slot is a falling meteor

    if (projStates[iid] == SWARM_DEAD)
        return o;
    SwarmProjectile p = projectiles[iid];
    SwarmMotion m = motions[p.motion & SWARM_MOTION_INDEX_MASK];
    if (m.mode != SWARM_MOTION_DROP)
        return o;

    float radius = (m.hitArea != 0u) ? areaDefs[m.hitArea].radius : 1.0;
    radius = max(radius, 0.01);
    float3 c3 = paths[iid].p3;
    float2 c = kCorner[vid];
    float3 world = float3(c3.x + c.x * radius,
                          c3.y - g_GroundY + g_RingLift,
                          c3.z + c.y * radius);

    o.pos = mul(mul(float4(world, 1.0), g_RingView), g_RingProj);
    o.uv = c;
    o.progress = saturate(p.pathT);
    o.edgeWidth = g_RingEdgeWidth / radius;
    return o;
}
