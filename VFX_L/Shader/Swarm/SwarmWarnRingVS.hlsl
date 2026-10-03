// ============================================================
// SwarmWarnRingVS.hlsl
// Warning rings the CPU places on the ground (2026-10-03: the boss's
// ground slam, SwarmSystem::SetWarnCircles). Same look as the bomber /
// meteor rings: the rim marks the blast radius, a disc grows from the
// centre with progress and touches the rim when it goes off.
// The pixel shader is SwarmBomberRingPS (same b0 layout); the colours are
// SwarmSystem::warnRing.
//
// One instance per circle. Unlike the meteor ring (one flat quad) each
// ring is a WARN_GRID x WARN_GRID grid whose vertices sit on the height
// field, so a ring on a ramp or across a step follows the ground. Heights
// are clamped to the centre's +-WARN_MAX_STEP so a ring hanging over a
// cliff does not climb the cliff face. No vertex buffer: SV_VertexID.
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

// SwarmSystem::WarnCircle (32 bytes)
struct SwarmWarnCircle
{
    float3 center;   // on the ground
    float radius;
    float progress;  // 0..1
    float3 _pad;
};

StructuredBuffer<SwarmWarnCircle> circles : register(t0);
StructuredBuffer<float> heights : register(t1);

static const uint WARN_GRID = 8u;
static const float WARN_MAX_STEP = 0.6;

struct RingOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;       // -1..1 across the radius
    float progress : TEXCOORD1;  // radius of the filled disc in uv units
    float edgeWidth : TEXCOORD2; // rim thickness in uv units
};

static const float2 kCorner[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

RingOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    RingOut o = (RingOut) 0;

    uint quad = vid / 6u;
    uint qx = quad % WARN_GRID;
    uint qz = quad / WARN_GRID;
    float2 uv = (float2((float) qx, (float) qz) + kCorner[vid % 6u]) / (float) WARN_GRID * 2.0 - 1.0;

    SwarmWarnCircle w = circles[iid];
    float radius = max(w.radius, 0.01);
    float2 xz = w.center.xz + uv * radius;
    float h = SwarmTerrainHeight(heights, xz);
    h = clamp(h, w.center.y - WARN_MAX_STEP, w.center.y + WARN_MAX_STEP);
    float3 world = float3(xz.x, h + g_RingLift, xz.y);

    o.pos = mul(mul(float4(world, 1.0), g_RingView), g_RingProj);
    o.uv = uv;
    o.progress = saturate(w.progress);
    o.edgeWidth = g_RingEdgeWidth / radius;
    return o;
}
