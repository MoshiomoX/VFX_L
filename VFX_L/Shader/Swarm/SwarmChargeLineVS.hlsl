// ============================================================
// SwarmChargeLineVS.hlsl
// The red band on the ground in front of a winding-up charger
// (2026-10-08): it shows exactly where the dash will go, so the player
// can step aside. One instance per charger (the charger draw list, the
// count comes from the list through DrawInstancedIndirect); chargers that
// are not winding up are collapsed.
//
// The band starts under the charger and runs along its yaw (locked when
// the wind-up began, see SwarmEnemyAICS) for the dash distance + a body
// radius. LINE_SEGS quads along it, every vertex on the height field.
// The fill grows from the charger to the tip with the wind-up; when it
// reaches the tip the charger goes.
//
// Pixel shader: SwarmChargeLinePS (same b0 as the bomber ring:
// SwarmSystem::BomberRingCB, colours SwarmSystem::chargeLine).
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
    float4 g_RingFill;
    float4 g_RingEdge;
    float4 g_RingBack;
    float g_RingEdgeWidth; // side rim, meters
    float g_RingLift;      // meters above the ground
    float2 _ringPad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> chargerList : register(t2);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3);
StructuredBuffer<float> heights : register(t4);

static const uint LINE_SEGS = 24u;
// band half width in body radii (a little wider than the body: that is what hits)
static const float LINE_WIDTH_MUL = 1.4;

struct LineOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;      // x = 0 at the charger .. 1 at the tip, y = -1 .. 1 across
    float progress : TEXCOORD1; // filled up to this x
    float2 size : TEXCOORD2;    // x = length m, y = side rim in uv units
};

static const float2 kCorner[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

LineOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    LineOut o = (LineOut) 0;
    uint slot = chargerList[iid];
    SwarmEnemyExtra ex = enemyExtra[slot];
    if (enemyStates[slot] == SWARM_DEAD || ex.kind != SWARM_KIND_CHARGER
        || SwarmChargerPhase(ex.fuse) != SWARM_CHARGE_WINDUP)
    {
        o.pos = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmEnemy e = enemies[slot];
    float ys, yc;
    sincos(e.yaw, ys, yc);
    float2 dir = float2(ys, yc);
    float2 side = float2(dir.y, -dir.x);
    float r = g_EnemyRadius * SwarmKindScale(ex.kind);
    float len = g_ChargerDashDist + r;
    float halfW = r * LINE_WIDTH_MUL;

    uint quad = vid / 6u;
    float2 c = kCorner[vid % 6u];
    float s = ((float) quad + c.x) / (float) LINE_SEGS;
    float across = c.y * 2.0 - 1.0;
    float2 xz = e.position.xz + dir * (s * len) + side * (across * halfW);
    float h = SwarmTerrainHeight(heights, xz);

    o.pos = mul(mul(float4(xz.x, h + g_RingLift, xz.y, 1.0), g_RingView), g_RingProj);
    o.uv = float2(s, across);
    o.progress = saturate(ex.fuse / max(g_ChargerWindup, 1e-3));
    o.size = float2(len, g_RingEdgeWidth / max(halfW, 1e-3));
    return o;
}
