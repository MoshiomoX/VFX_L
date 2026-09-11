// ============================================================
// SwarmDebugEnemyVS.hlsl
// Debug wireframe for GPU enemies: a vertical capsule built from
// 64 line segments (128 vertices) per instance.
//   [  0, 64)  top ring + bottom ring, 16 segments each
//   [ 64, 72)  4 vertical lines
//   [ 72,120)  4 semicircle arcs (top/bottom x two planes), 6 segs each
//   [120,128)  degenerate padding
//
// Radius and segment half-length come from SwarmAICB so the frame
// always matches what HitCS / ContactCS actually test against.
// ============================================================

#define SWARM_FRAME_CB_REG b2
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmDebugCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

static const float PI = 3.14159265;
static const float TWO_PI = 6.28318531;
static const uint RING_SEGS = 16;
static const uint ARC_SEGS = 6;

// local-space offset of vertex vid on a capsule of radius r whose
// straight segment runs from -h to +h on the y axis
float3 CapsulePoint(uint vid, float r, float h)
{
    // ---- rings ----
    if (vid < 64)
    {
        uint ring = vid / 32; // 0 = top, 1 = bottom
        uint k = vid % 32;
        uint seg = k / 2;
        uint endp = k & 1;
        float a = (float) (seg + endp) * TWO_PI / (float) RING_SEGS;
        float y = (ring == 0) ? h : -h;
        return float3(cos(a) * r, y, sin(a) * r);
    }

    // ---- verticals ----
    if (vid < 72)
    {
        uint k = vid - 64;
        uint idx = k / 2; // 0..3
        uint endp = k & 1;
        float a = (float) idx * PI * 0.5;
        float y = (endp == 0) ? h : -h;
        return float3(cos(a) * r, y, sin(a) * r);
    }

    // ---- cap arcs ----
    if (vid < 120)
    {
        uint k = vid - 72; // 0..47
        uint arc = k / 12; // 0 topXY, 1 topZY, 2 botXY, 3 botZY
        uint m = k % 12;
        uint seg = m / 2;
        uint endp = m & 1;
        float t = (float) (seg + endp) * PI / (float) ARC_SEGS; // 0..PI
        float c = cos(t);
        float s = sin(t);

        bool top = (arc < 2);
        bool planeXY = ((arc & 1) == 0);

        float y = top ? (h + s * r) : (-h - s * r);
        return planeXY ? float3(c * r, y, 0.0) : float3(0.0, y, c * r);
    }

    // ---- padding: collapse to a point ----
    return float3(0.0, h, 0.0);
}

VSOutput main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOutput o = (VSOutput) 0;

    if (enemyStates[iid] == SWARM_DEAD)
    {
        o.position = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmEnemy e = enemies[iid];

    float3 worldPos = e.position
                    + CapsulePoint(vid, g_EnemyRadius, g_EnemyCapsuleHalf);

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    o.position = mul(viewPos, g_Projection);

    o.color = float4(0.4, 0.6, 1.0, 1.0);
    return o;
}