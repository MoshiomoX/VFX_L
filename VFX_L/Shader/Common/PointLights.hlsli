// ============================================================
// PointLights.hlsli
// Per-frame point light list, shared by every model PS.
//
// The list is one buffer for the whole frame:
//   [0, cpuCount)        written by PointLightManager (CPU VFX entries,
//                        editor preview, area casts, elites)
//   [cpuCount, count)    appended by SwarmLightCollectCS from live GPU
//                        projectiles / areas whose recipe has a light
// count lives in a 1-element buffer so the PS loops only over live
// lights. Unbound SRVs read as 0, so a scene without lights costs
// one buffer read.
//
// C++ mirror: Graphics/Light/PointLightManager.h (PointLight, 32 bytes)
// ============================================================
#ifndef POINT_LIGHTS_HLSLI
#define POINT_LIGHTS_HLSLI

#define MAX_POINT_LIGHTS 64u

struct PointLight
{
    float3 position;
    float radius;      // meters. Zero light past this
    float3 color;
    float intensity;   // linear HDR gain (>1 blooms)
};

#ifndef POINT_LIGHT_SRV_REG
#define POINT_LIGHT_SRV_REG t6
#endif
#ifndef POINT_LIGHT_COUNT_SRV_REG
#define POINT_LIGHT_COUNT_SRV_REG t7
#endif

StructuredBuffer<PointLight> g_PointLights : register(POINT_LIGHT_SRV_REG);
StructuredBuffer<uint> g_PointLightCount : register(POINT_LIGHT_COUNT_SRV_REG);

// smooth windowed inverse-square falloff (reaches exactly 0 at radius)
float PointLightAttenuation(float dist, float radius)
{
    float x = saturate(1.0 - (dist * dist) / max(radius * radius, 1e-4));
    return x * x;
}

// Sum of N.L-weighted point light radiance at worldPos. Multiply by albedo
float3 PointLightDiffuse(float3 worldPos, float3 N)
{
    uint count = min(g_PointLightCount[0], MAX_POINT_LIGHTS);
    float3 sum = float3(0, 0, 0);
    for (uint i = 0u; i < count; ++i)
    {
        PointLight l = g_PointLights[i];
        float3 d = l.position - worldPos;
        float dist = length(d);
        if (dist >= l.radius)
            continue;
        float NdotL = max(dot(N, d / max(dist, 1e-4)), 0.0);
        sum += l.color * (l.intensity * PointLightAttenuation(dist, l.radius) * NdotL);
    }
    return sum;
}

#endif
