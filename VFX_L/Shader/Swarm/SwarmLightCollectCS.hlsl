// ============================================================
// SwarmLightCollectCS.hlsl
// One thread per GPU projectile. If alive and its recipe carries a
// light, append a PointLight at the projectile's position to the
// frame's light list (PointLights.hlsli). The list already holds the
// CPU-side lights; the counter continues from there.
//
// The list is capped (MAX_POINT_LIGHTS). Slots are first come first
// served -- with hundreds of fireballs only the first 64 shine, which
// is fine: the PS could not afford more anyway.
//
// SwarmAreaLightCollectCS.hlsl #defines SWARM_LIGHT_AREAS and includes
// this file: same code, sources are the live areas.
// ============================================================
#include "../Common/SwarmCommon.hlsli"
#include "../Common/PointLights.hlsli"   // struct PointLight (SRVs unused here)

// Must match Swarm::VFXLightEntry in SwarmVFXTable.h (32 bytes)
struct SwarmLightEntry
{
    float4 color;
    float radius;
    float intensity;
    float2 _pad;
};

#ifdef SWARM_LIGHT_AREAS
StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
#else
StructuredBuffer<SwarmProjectile> projectiles : register(t0);
Buffer<uint> projStates : register(t1);
#endif
StructuredBuffer<SwarmRecipe> recipes : register(t2);
StructuredBuffer<SwarmLightEntry> lightDefs : register(t3);

RWStructuredBuffer<PointLight> outLights : register(u0);
RWStructuredBuffer<uint> outCount : register(u1);   // [0] = live count

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;

    float3 srcPos;
    uint srcVfx;
#ifdef SWARM_LIGHT_AREAS
    if (i >= SWARM_MAX_AREAS)
        return;
    if (areaStates[i] == SWARM_DEAD)
        return;
    srcPos = areas[i].center;
    srcVfx = areas[i].vfxType;
#else
    if (i >= g_MaxProjectiles)
        return;
    if (projStates[i] == SWARM_DEAD)
        return;
    srcPos = projectiles[i].position;
    srcVfx = projectiles[i].vfxType;
#endif

    SwarmRecipe r = recipes[srcVfx];
    if (r.lightCount == 0u)
        return;

    for (uint k = 0u; k < r.lightCount; ++k)
    {
        uint slot;
        InterlockedAdd(outCount[0], 1u, slot);
        if (slot >= MAX_POINT_LIGHTS)
        {
            // over budget: give the slot back so the count stays <= MAX
            InterlockedAdd(outCount[0], (uint) -1, slot);
            return;
        }
        SwarmLightEntry d = lightDefs[r.lightStart + k];
        PointLight l;
        l.position = srcPos;
        l.radius = d.radius;
        l.color = d.color.rgb;
        l.intensity = d.intensity;
        outLights[slot] = l;
    }
}
