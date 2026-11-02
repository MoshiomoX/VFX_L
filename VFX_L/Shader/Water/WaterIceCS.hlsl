// ============================================================
// WaterIceCS.hlsl
// Ice on the ponds (World/WaterSurface, 2026-11-01). One value per
// 0.8 m texel over the whole map: 1 = just frozen, falling to 0 over
// g_IceLife seconds. A live cold area freezes the pond texels it covers:
// a recipe with negative heat (the Ice Lance's hit) or any freezing area
// (the Ice Spike's row, which comes from the CPU with no recipe).
// Dry texels stay 0. Same rule as the fire's cold sources (GrassFireCS).
// WaterRippleCS treats ice as a wall, WaterPS draws it, and WaterStirCS
// slows the enemies walking on it.
// ============================================================

// Must match WaterSurface::IceCB
cbuffer WaterIceCB : register(b0)
{
    float2 g_MapOrigin;     // world xz of the map's min corner
    float2 g_MapSize;       // meters
    float g_Dt;             // seconds this frame (0 while paused)
    float g_IceLife;        // seconds a frozen texel lasts
    float2 _icePad;
};

// = SwarmArea (Shader/Common/SwarmCommon.hlsli), 48 bytes
struct IceArea
{
    float3 center;
    float radius;
    float damage;
    float timeLeft;
    float tickInterval;
    float tickTimer;
    float halfHeight;
    uint flags;
    uint vfxType;
    uint tickNow;
};

Texture2D<float> g_Mask : register(t0);          // 1 = pond (the ripple field's 1024^2 texels)
StructuredBuffer<IceArea> g_Areas : register(t1);
Buffer<uint> g_AreaStates : register(t2);        // 0 = dead
StructuredBuffer<float> g_RecipeHeat : register(t3);   // per VFX recipe (GrassFireField), < 0 = cold
RWTexture2D<float> g_Ice : register(u0);

#define ICE_MAX_AREAS 256u       // = SWARM_MAX_AREAS
#define ICE_MAX_SOURCES 32u
#define ICE_MASK_SCALE 4         // texels of the mask per ice texel
#define ICE_FREEZE_MASK 0xF0u    // = Swarm::kAreaFreezeShift, 4 bits (SwarmAreaFreeze)
#define ICE_MIN_RADIUS 1.2       // an Ice Lance's hit is only 0.5 m: still a visible patch of ice

groupshared float3 gs_Source[ICE_MAX_SOURCES];   // xz, radius
groupshared uint gs_SourceCount;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    if (gi == 0u)
        gs_SourceCount = 0u;
    GroupMemoryBarrierWithGroupSync();

    uint heatCount, heatStride;
    g_RecipeHeat.GetDimensions(heatCount, heatStride);
    for (uint a = gi; a < ICE_MAX_AREAS; a += 64u)
    {
        if (g_AreaStates[a] == 0u)
            continue;
        IceArea ar = g_Areas[a];
        bool cold = (ar.flags & ICE_FREEZE_MASK) != 0u
            || (ar.vfxType < heatCount && g_RecipeHeat[ar.vfxType] < 0.0);
        if (!cold)
            continue;
        uint slot;
        InterlockedAdd(gs_SourceCount, 1u, slot);
        if (slot < ICE_MAX_SOURCES)
            gs_Source[slot] = float3(ar.center.xz, max(ar.radius, ICE_MIN_RADIUS));
    }
    GroupMemoryBarrierWithGroupSync();

    uint w, h;
    g_Ice.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;
    const int2 p = int2(id.xy);
    if (g_Mask.Load(int3(p * ICE_MASK_SCALE + ICE_MASK_SCALE / 2, 0)) < 0.5)
    {
        g_Ice[p] = 0.0;
        return;
    }

    float v = max(g_Ice[p] - g_Dt / max(g_IceLife, 0.1), 0.0);
    const float2 xz = g_MapOrigin + (float2(p) + 0.5) / float2(w, h) * g_MapSize;
    const uint n = min(gs_SourceCount, ICE_MAX_SOURCES);
    for (uint i = 0u; i < n; ++i)
        if (distance(xz, gs_Source[i].xy) < gs_Source[i].z)
            v = 1.0;
    g_Ice[p] = v;
}
