// ============================================================
// WaterRippleCS.hlsl
// One step of the pond ripples (World/WaterSurface, 2026-10-27).
// A height field over the whole map, 1024^2 (0.2 m per texel):
// x = height now, y = height one step ago. Ping-pong like the trample map.
// Only texels inside a pond (g_Mask) move; dry texels stay 0 and act as
// a wall for the waves next to them (they read back the water's own
// height = waves bounce off the shore).
//
// Per step (a damped wave equation):
//   next = cur + (cur - prev) * keep + courant2 * laplacian - cur * restore
// The things that push the water are added on the first step of a frame
// only (g_Sources):
//   CPU pushes   footsteps / landings / wading (WaterSurface::AddRipple)
//   GPU areas    an area that ticks for the first time (tickNow bit 1):
//                blasts (SWARM_AREA_SHAKE) push hard, the rest a little.
//                Capsules (beams) are skipped. The group compacts the
//                live first-tick areas over its tile into shared memory
//   rain         a drop per rain cell with chance g_RainProb this step
// Nothing else moves the water: without a cause it stays flat.
// ============================================================

// Must match WaterSurface::RippleCB
cbuffer WaterRippleCB : register(b0)
{
    float2 g_MapOrigin;     // world xz of the map's min corner
    float2 g_MapSize;       // meters
    float g_Courant2;       // (wave speed x dt / texel)^2
    float g_Keep;           // velocity kept per step (damping)
    float g_Restore;        // pull toward 0 per step
    float g_Texel;          // meters per texel
    uint g_Step;            // step counter (rain hash)
    uint g_Sources;         // 1 = add the pushes this step
    float g_RainProb;       // chance of a drop per rain cell this step
    float g_RainCell;       // meters
    float g_RainStrength;   // m
    float g_RainRadius;     // m
    float g_BlastStrength;  // m (blast areas)
    float g_AreaStrength;   // m (other areas)
    uint g_ImpulseCount;
    float3 _ripplePad;
    float4 g_Impulse[16];   // x, z, radius, strength (m, + = pushed down)
};

// = SwarmArea (Shader/Common/SwarmCommon.hlsli), 48 bytes
struct RippleArea
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

Texture2D<float2> g_In : register(t0);
Texture2D<float> g_Mask : register(t1);          // 1 = pond
StructuredBuffer<RippleArea> g_Areas : register(t2);
Buffer<uint> g_AreaStates : register(t3);        // 0 = dead
RWTexture2D<float2> g_Out : register(u0);

#define RIPPLE_SIZE 1024
#define RIPPLE_MAX_AREAS 256u    // = SWARM_MAX_AREAS
#define RIPPLE_MAX_SOURCES 32u
#define RIPPLE_AREA_CAPSULE 4u   // = SWARM_AREA_CAPSULE
#define RIPPLE_AREA_SHAKE 8u     // = SWARM_AREA_SHAKE
#define RIPPLE_TICK_FIRST 2u     // = SWARM_AREA_TICK_FIRST

groupshared float4 gs_Source[RIPPLE_MAX_SOURCES];   // xz, radius, strength
groupshared uint gs_SourceCount;

uint RippleHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float RippleRand(uint x)
{
    return (float) (RippleHash(x) & 0xFFFFFFu) / 16777216.0;
}

// smooth bump: 1 at the centre, 0 at r
float Bump(float d, float r)
{
    float t = saturate(1.0 - (d * d) / max(r * r, 1e-6));
    return t * t;
}

float LoadH(int2 p, float self)
{
    p = clamp(p, int2(0, 0), int2(RIPPLE_SIZE - 1, RIPPLE_SIZE - 1));
    return g_Mask.Load(int3(p, 0)) > 0.5 ? g_In.Load(int3(p, 0)).x : self;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gi : SV_GroupIndex)
{
    // ---- the live first-tick areas that touch this 8x8 tile ----
    if (gi == 0)
        gs_SourceCount = 0;
    GroupMemoryBarrierWithGroupSync();
    if (g_Sources != 0u)
    {
        float2 tileMin = g_MapOrigin + float2(gid.xy * 8u) * g_Texel;
        float2 tileMax = tileMin + 8.0 * g_Texel;
        for (uint k = gi; k < RIPPLE_MAX_AREAS; k += 64u)
        {
            if (g_AreaStates[k] == 0u)
                continue;
            RippleArea a = g_Areas[k];
            if ((a.tickNow & RIPPLE_TICK_FIRST) == 0u || (a.flags & RIPPLE_AREA_CAPSULE) != 0u || a.radius <= 0.0)
                continue;
            float2 nearest = clamp(a.center.xz, tileMin, tileMax);
            if (distance(nearest, a.center.xz) > a.radius)
                continue;
            float s = ((a.flags & RIPPLE_AREA_SHAKE) != 0u) ? g_BlastStrength : g_AreaStrength;
            uint slot;
            InterlockedAdd(gs_SourceCount, 1u, slot);
            if (slot < RIPPLE_MAX_SOURCES)
                gs_Source[slot] = float4(a.center.xz, a.radius, s);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (any(id.xy >= uint2(RIPPLE_SIZE, RIPPLE_SIZE)))
        return;
    int2 p = int2(id.xy);
    if (g_Mask.Load(int3(p, 0)) < 0.5)
    {
        g_Out[p] = float2(0.0, 0.0);
        return;
    }

    float2 s = g_In.Load(int3(p, 0));
    float cur = s.x;
    float prev = s.y;
    float lap = LoadH(p + int2(1, 0), cur) + LoadH(p - int2(1, 0), cur)
              + LoadH(p + int2(0, 1), cur) + LoadH(p - int2(0, 1), cur) - 4.0 * cur;
    float next = cur + (cur - prev) * g_Keep + g_Courant2 * lap - cur * g_Restore;

    if (g_Sources != 0u)
    {
        float2 xz = g_MapOrigin + (float2(p) + 0.5) * g_Texel;
        // the pushes move this step's and the last step's height together: they change the shape
        // of the surface, not its speed (a speed kick keeps sinking the water for ~1/(1-keep) steps)
        float push = 0.0;
        // CPU pushes (feet, landings)
        for (uint i = 0u; i < g_ImpulseCount; ++i)
        {
            float4 im = g_Impulse[i];
            push += im.w * Bump(distance(xz, im.xy), im.z);
        }
        // areas that just appeared (blasts, potion splashes)
        uint n = min(gs_SourceCount, RIPPLE_MAX_SOURCES);
        for (uint j = 0u; j < n; ++j)
        {
            float4 a = gs_Source[j];
            // a dip in the middle plus a ring at 0.6 of the radius: the wave runs out from the blast
            float d = distance(xz, a.xy);
            push += a.w * (Bump(d, a.z * 0.6) + 0.5 * Bump(abs(d - a.z * 0.6), max(a.z * 0.25, g_Texel * 2.0)));
        }
        // rain: at most one drop per rain cell, at a random point of the cell
        if (g_RainProb > 0.0)
        {
            int2 cell = int2(floor(xz / g_RainCell));
            [unroll]
            for (int dz = -1; dz <= 1; ++dz)
            {
                [unroll]
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int2 c = cell + int2(dx, dz);
                    uint seed = RippleHash((uint) c.x * 73856093u ^ (uint) c.y * 19349663u ^ g_Step * 83492791u);
                    if (RippleRand(seed) >= g_RainProb)
                        continue;
                    float2 drop = (float2(c) + float2(RippleRand(seed + 1u), RippleRand(seed + 2u))) * g_RainCell;
                    push += g_RainStrength * Bump(distance(xz, drop), g_RainRadius);
                }
            }
        }
        next -= push;
        cur -= push;
    }
    g_Out[p] = float2(next, cur);
}
