// ============================================================
// SwarmDebugProjVS.hlsl
// Debug wireframe for GPU projectile cores.
//
// No readback: the VS reads the projectile buffer directly and
// expands each alive slot into two rings (XZ + XY) as a line list.
// Dead slots are collapsed behind the near plane.
//
// 64 vertices per instance = 2 rings x 16 segments x 2 endpoints.
// ============================================================

// SwarmCommon owns b0 by default; move it out of the way (unused here)
#define SWARM_FRAME_CB_REG b1
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmDebugCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
};

StructuredBuffer<SwarmProjectile> projectiles : register(t0);
Buffer<uint> projStates : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

static const uint SEGS = 16;
static const float TWO_PI = 6.28318530718;

VSOutput main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOutput o = (VSOutput) 0;

    if (projStates[iid] == SWARM_DEAD)
    {
        o.position = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmProjectile p = projectiles[iid];

    // which ring, which segment, which endpoint
    uint ring = vid / (SEGS * 2); // 0 = XZ, 1 = XY
    uint seg = (vid % (SEGS * 2)) / 2;
    uint endp = vid & 1;
    float a = (float) (seg + endp) * TWO_PI / (float) SEGS;

    float3 offset;
    if (ring == 0)
        offset = float3(cos(a), 0.0, sin(a)) * p.radius;
    else
        offset = float3(cos(a), sin(a), 0.0) * p.radius;

    float3 worldPos = p.position + offset;

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    o.position = mul(viewPos, g_Projection);

    // green normally, turns red in the last 0.3s of life
    o.color = (p.lifetime < 0.3) ? float4(1.0, 0.3, 0.3, 1.0)
                                 : float4(0.4, 1.0, 0.4, 1.0);
    return o;
}