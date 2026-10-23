// ============================================================
// LightClusters.hlsli
// Clustered forward shading ("Forward+", 2026-10-23).
//
// The camera frustum is cut into LIGHT_CLUSTER_X x LIGHT_CLUSTER_Y
// screen tiles x LIGHT_CLUSTER_Z depth slices (exponential in depth).
// Once a frame, after the light list is complete (CPU lights + the GPU
// projectiles / areas), LightClusterCS writes for every cluster a bit
// mask of the point lights whose sphere touches it.
// PointLightShade (Lighting.hlsli) projects the pixel into its cluster
// and loops only over the set bits instead of the whole list. Lights
// that reach no pixel cost nothing any more.
//
// The cluster info is bound beside the masks; when it is not built this
// frame (editor scenes without a camera, or switched off) the SRVs are
// null, the info reads as 0 (enabled = 0) and the PS falls back to the
// old loop over the whole list.
//
// C++ mirror: Graphics/Light/LightClusters.h (LightClusterInfo, 160 bytes)
// ============================================================
#ifndef LIGHT_CLUSTERS_HLSLI
#define LIGHT_CLUSTERS_HLSLI

#include "PointLights.hlsli"   // PointLight, MAX_POINT_LIGHTS

#define LIGHT_CLUSTER_X 16
#define LIGHT_CLUSTER_Y 9
#define LIGHT_CLUSTER_Z 24
#define LIGHT_CLUSTER_COUNT (LIGHT_CLUSTER_X * LIGHT_CLUSTER_Y * LIGHT_CLUSTER_Z)
#define LIGHT_CLUSTER_WORDS (MAX_POINT_LIGHTS / 32u)   // one bit per light

struct LightClusterInfo
{
    // rows of view * proj, row-vector convention: clip = x*r0 + y*r1 + z*r2 + r3
    float4 viewProj0;
    float4 viewProj1;
    float4 viewProj2;
    float4 viewProj3;
    // rows of the view matrix (right handed: in front of the camera = -z)
    float4 view0;
    float4 view1;
    float4 view2;
    float4 view3;
    float2 projScale;   // proj._11, proj._22: view x (y) at depth d = ndc * d / scale
    float  zNear;       // first slice boundary (m). Slice 0 still starts at the camera
    float  zFar;        // last slice boundary (m). The last slice runs to infinity
    float  sliceScale;  // LIGHT_CLUSTER_Z / ln(zFar / zNear)
    float  sliceBias;   // -ln(zNear) * sliceScale: slice = floor(ln(depth) * scale + bias)
    uint   enabled;     // 0 = not built this frame: loop over the whole list
    uint   debugView;   // 1 = heat map of the lights each pixel visits
};

// the slice boundary k (k = 0..LIGHT_CLUSTER_Z) as a view depth
// (single exit: fxc reports early returns as "potentially uninitialized")
float LightClusterSliceDepth(LightClusterInfo info, uint k)
{
    float d = info.zNear * exp((float) k / info.sliceScale);
    d = (k == 0u) ? 0.0 : d;
    d = (k >= (uint) LIGHT_CLUSTER_Z) ? 1.0e5 : d;
    return d;
}

#ifndef LIGHT_CLUSTER_NO_PS_SRVS
StructuredBuffer<uint> g_LightClusterMasks : register(t10);
StructuredBuffer<LightClusterInfo> g_LightClusterInfo : register(t11);

// the cluster a world position falls into (index into the masks / LIGHT_CLUSTER_WORDS)
uint LightClusterOf(LightClusterInfo info, float3 worldPos)
{
    float4 clip = worldPos.x * info.viewProj0 + worldPos.y * info.viewProj1
                + worldPos.z * info.viewProj2 + info.viewProj3;
    float depth = max(clip.w, 1e-4);   // = view depth for a perspective projection
    float2 ndc = clip.xy / depth;
    int tx = clamp((int) floor((ndc.x * 0.5 + 0.5) * LIGHT_CLUSTER_X), 0, LIGHT_CLUSTER_X - 1);
    int ty = clamp((int) floor((0.5 - ndc.y * 0.5) * LIGHT_CLUSTER_Y), 0, LIGHT_CLUSTER_Y - 1);
    int tz = clamp((int) floor(log(depth) * info.sliceScale + info.sliceBias), 0, LIGHT_CLUSTER_Z - 1);
    return (uint) ((tz * LIGHT_CLUSTER_Y + ty) * LIGHT_CLUSTER_X + tx);
}

// debug view: lights visited -> blue (1) .. green (4) .. red (16+)
float3 LightClusterHeat(uint lights)
{
    float t = saturate(log2((float) max(lights, 1u)) / 4.0);
    float3 c = t < 0.5 ? lerp(float3(0.1, 0.3, 1.0), float3(0.1, 1.0, 0.2), t * 2.0)
                       : lerp(float3(0.1, 1.0, 0.2), float3(1.0, 0.1, 0.05), t * 2.0 - 1.0);
    return lights == 0u ? float3(0, 0, 0) : c * 0.35;
}
#endif

#endif
