// ============================================================
// LightClusterCS.hlsl
// Forward+ light culling (2026-10-23, LightClusters.hlsli).
// One thread per cluster. The group first moves the frame's point
// lights into view space (groupshared), then every thread builds its
// cluster's view-space box and tests each light sphere against it,
// one 32-bit mask word at a time.
//
// Runs once a frame after the light list is complete (SceneBase::Render
// -> PointLightManager::BuildClusters), with the camera the scene draws with.
// ============================================================
#define LIGHT_CLUSTER_NO_PS_SRVS
#include "../Common/LightClusters.hlsli"

StructuredBuffer<PointLight> g_Lights : register(t0);
StructuredBuffer<uint> g_LightCount : register(t1);
StructuredBuffer<LightClusterInfo> g_Info : register(t2);
RWStructuredBuffer<uint> g_Masks : register(u0);

#define GROUP 64u

groupshared float4 s_Lights[MAX_POINT_LIGHTS];   // xyz = view-space centre, w = radius

[numthreads(GROUP, 1, 1)]
void main(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex)
{
    LightClusterInfo info = g_Info[0];
    uint n = min(g_LightCount[0], MAX_POINT_LIGHTS);
    for (uint li = gi; li < n; li += GROUP)
    {
        PointLight l = g_Lights[li];
        float3 p = l.position;
        float3 v = p.x * info.view0.xyz + p.y * info.view1.xyz + p.z * info.view2.xyz + info.view3.xyz;
        // a few cm more: a pixel right on a cluster border may land in either cluster
        s_Lights[li] = float4(v, l.radius + 0.05);
    }
    GroupMemoryBarrierWithGroupSync();

    uint c = gid.x * GROUP + gi;
    if (c >= (uint) LIGHT_CLUSTER_COUNT)
        return;

    uint tx = c % (uint) LIGHT_CLUSTER_X;
    uint ty = (c / (uint) LIGHT_CLUSTER_X) % (uint) LIGHT_CLUSTER_Y;
    uint tz = c / (uint) (LIGHT_CLUSTER_X * LIGHT_CLUSTER_Y);

    // the cluster in NDC (y: tile 0 is the top row) and in view depth
    float nx0 = (float) tx / LIGHT_CLUSTER_X * 2.0 - 1.0;
    float nx1 = (float) (tx + 1u) / LIGHT_CLUSTER_X * 2.0 - 1.0;
    float ny1 = 1.0 - (float) ty / LIGHT_CLUSTER_Y * 2.0;
    float ny0 = 1.0 - (float) (ty + 1u) / LIGHT_CLUSTER_Y * 2.0;
    float d0 = LightClusterSliceDepth(info, tz);
    float d1 = LightClusterSliceDepth(info, tz + 1u);

    // view-space box: x = ndc * depth / scale, so the extremes sit at the near or far end
    float3 bmin, bmax;
    bmin.x = min(nx0 * d0, nx0 * d1) / info.projScale.x;
    bmax.x = max(nx1 * d0, nx1 * d1) / info.projScale.x;
    bmin.y = min(ny0 * d0, ny0 * d1) / info.projScale.y;
    bmax.y = max(ny1 * d0, ny1 * d1) / info.projScale.y;
    bmin.z = -d1;   // right handed: in front = -z
    bmax.z = -d0;

    uint base = c * LIGHT_CLUSTER_WORDS;
    for (uint w = 0u; w < LIGHT_CLUSTER_WORDS; ++w)
    {
        uint mask = 0u;
        uint start = w * 32u;
        uint end = min(start + 32u, n);
        for (uint i = start; i < end; ++i)
        {
            float4 s = s_Lights[i];
            float3 d = s.xyz - clamp(s.xyz, bmin, bmax);
            if (dot(d, d) <= s.w * s.w)
                mask |= 1u << (i - start);
        }
        g_Masks[base + w] = mask;
    }
}
