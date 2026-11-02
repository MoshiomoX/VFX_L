// ============================================================
// WaterRippleCS.hlsl
// One step of the pond ripples (World/WaterSurface, 2026-10-27).
// A height field over the whole map, 1024^2 (0.2 m per texel):
// x = height now, y = height one step ago. Ping-pong like the trample map.
// zw = the same for the big waves only (blasts, the boss's slams and rocks):
// the waves add up, so zw is their share of xy. WaterPS draws the white
// crest lines from zw, so the player's own steps and landings never do
// (the user: "there are still small circles").
// Only texels inside a pond (g_Mask) move; dry texels stay 0 and act as
// a wall for the waves next to them (they read back the water's own
// height = waves bounce off the shore).
//
// Per step (a damped wave equation):
//   next = cur + (cur - prev) * keep + courant2 * laplacian - cur * restore
// (restore is tiny: the pushes move no water, so it only clears rounding drift)
// The things that push the water are added on the first step of a frame
// only (g_Sources):
//   CPU pushes   footsteps / landings / wading (WaterSurface::AddRipple;
//                the boss's slams and rocks are big waves)
//   GPU areas    an area the water has not seen yet (g_SeenIn):
//                blasts (SWARM_AREA_SHAKE) push hard as big waves, the rest
//                a little. Capsules (beams) are skipped. The group compacts
//                the new areas over its tile into shared memory
//   enemies      wading enemies (WaterStirCS writes the list each frame,
//                gathered the same way as the areas)
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
    float4 g_Impulse[16];   // x, z, radius (negative = a big wave), strength (m, + = pressed down in the middle)
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

Texture2D<float4> g_In : register(t0);          // xy = all waves (now, last step), zw = the big waves only
Texture2D<float> g_Mask : register(t1);          // 1 = pond
StructuredBuffer<RippleArea> g_Areas : register(t2);
Buffer<uint> g_AreaStates : register(t3);        // 0 = dead
Buffer<float> g_SeenIn : register(t4);           // per area slot: its timeLeft when the water last looked (0 = empty)
StructuredBuffer<float4> g_Stirs : register(t5); // enemies wading (WaterStirCS): x, z, radius, strength
Buffer<uint> g_StirCount : register(t6);         // [0] = how many WaterStirCS wrote (may exceed the list)
Texture2D<float> g_Ice : register(t7);           // ice over the ponds (WaterIceCS), a quarter of this size
RWTexture2D<float4> g_Out : register(u0);
RWBuffer<float> g_SeenOut : register(u1);        // the same for the next frame (group 0 writes it on the first step)

#define RIPPLE_SIZE 1024
#define RIPPLE_MAX_AREAS 256u    // = SWARM_MAX_AREAS
#define RIPPLE_MAX_SOURCES 32u
#define RIPPLE_MAX_STIRS 128u    // = WaterSurface::kMaxStirs
#define RIPPLE_AREA_CAPSULE 4u   // = SWARM_AREA_CAPSULE
#define RIPPLE_AREA_SHAKE 8u     // = SWARM_AREA_SHAKE
#define RIPPLE_ICE_SCALE 4u      // ripple texels per ice texel (WaterSurface::kIceSize)
#define RIPPLE_ICE_SOLID 0.05    // frozen from here: waves stop and bounce off it like a shore

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

// a push that moves no water: pressed down 0.75 in the middle, lifted around it out to 2r
// (the bump's volume is pi r^2 / 3, so a quarter of the double-size bump cancels it).
// The lifted rim runs outward as a ring and the hole fills from the sides. A plain dip sank
// the whole pond, and the strong pull back to 0 that undid it held the long waves in place:
// they rose and fell where they were instead of spreading (outlined blobs, not rings)
float Splash(float d, float r)
{
    return Bump(d, r) - 0.25 * Bump(d, 2.0 * r);
}

// the neighbour's height now (x = all waves, y = the big ones); a dry neighbour gives back our own
float2 LoadH(int2 p, float2 self)
{
    p = clamp(p, int2(0, 0), int2(RIPPLE_SIZE - 1, RIPPLE_SIZE - 1));
    bool water = g_Mask.Load(int3(p, 0)) > 0.5 && g_Ice.Load(int3(int2(uint2(p) / RIPPLE_ICE_SCALE), 0)) < RIPPLE_ICE_SOLID;
    return water ? g_In.Load(int3(p, 0)).xz : self;
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
        bool firstGroup = all(gid.xy == uint2(0, 0));
        for (uint k = gi; k < RIPPLE_MAX_AREAS; k += 64u)
        {
            bool alive = g_AreaStates[k] != 0u;
            RippleArea a = g_Areas[k];
            // new = the slot was empty last time, or its time went up (a new area in a reused slot).
            // Not tickNow's "first tick": that lasts one swarm step, and when the swarm took two steps
            // in one frame (a heavy frame) it was gone before the water looked = a blast with no ripple
            float seen = g_SeenIn[k];
            if (firstGroup)
                g_SeenOut[k] = alive ? max(a.timeLeft, 1e-4) : 0.0;
            if (!alive || (seen > 0.0 && a.timeLeft <= seen + 1e-4))
                continue;
            if ((a.flags & RIPPLE_AREA_CAPSULE) != 0u || a.radius <= 0.0)
                continue;
            // the push reaches 1.2 x the radius (Splash at 0.6 R lifts out to 2 x 0.6 R)
            float2 nearest = clamp(a.center.xz, tileMin, tileMax);
            if (distance(nearest, a.center.xz) > a.radius * 1.2)
                continue;
            bool blast = (a.flags & RIPPLE_AREA_SHAKE) != 0u;
            float s = blast ? g_BlastStrength : g_AreaStrength;
            uint slot;
            InterlockedAdd(gs_SourceCount, 1u, slot);
            if (slot < RIPPLE_MAX_SOURCES)
                gs_Source[slot] = float4(a.center.xz, blast ? -a.radius : a.radius, s);   // negative radius = a big wave
        }
        // enemies wading (WaterStirCS). Stored as an area of radius r / 0.6, so the shared formula below
        // gives Splash(d, r)
        uint stirs = min(g_StirCount[0], RIPPLE_MAX_STIRS);
        for (uint m = gi; m < stirs; m += 64u)
        {
            float4 w = g_Stirs[m];
            float2 nearest = clamp(w.xy, tileMin, tileMax);
            if (distance(nearest, w.xy) > w.z * 2.0)
                continue;
            uint slot;
            InterlockedAdd(gs_SourceCount, 1u, slot);
            if (slot < RIPPLE_MAX_SOURCES)
                gs_Source[slot] = float4(w.xy, w.z / 0.6, w.w);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (any(id.xy >= uint2(RIPPLE_SIZE, RIPPLE_SIZE)))
        return;
    int2 p = int2(id.xy);
    if (g_Mask.Load(int3(p, 0)) < 0.5 || g_Ice.Load(int3(int2(uint2(p) / RIPPLE_ICE_SCALE), 0)) >= RIPPLE_ICE_SOLID)
    {
        g_Out[p] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // x = all waves, y = the big waves only (the same equation on both)
    float4 s = g_In.Load(int3(p, 0));
    float2 cur = s.xz;
    float2 prev = s.yw;
    float2 lap = LoadH(p + int2(1, 0), cur) + LoadH(p - int2(1, 0), cur)
               + LoadH(p + int2(0, 1), cur) + LoadH(p - int2(0, 1), cur) - 4.0 * cur;
    float2 next = cur + (cur - prev) * g_Keep + g_Courant2 * lap - cur * g_Restore;

    if (g_Sources != 0u)
    {
        float2 xz = g_MapOrigin + (float2(p) + 0.5) * g_Texel;
        // the pushes move this step's and the last step's height together: they change the shape
        // of the surface, not its speed (a speed kick keeps sinking the water for ~1/(1-keep) steps)
        float push = 0.0;
        float bigPush = 0.0;   // the part of it from the big waves (a negative radius)
        // CPU pushes (feet, landings; the boss's slams and rocks are big)
        for (uint i = 0u; i < g_ImpulseCount; ++i)
        {
            float4 im = g_Impulse[i];
            float v = im.w * Splash(distance(xz, im.xy), abs(im.z));
            push += v;
            bigPush += (im.z < 0.0) ? v : 0.0;
        }
        // areas that just appeared (blasts = big, potion splashes)
        uint n = min(gs_SourceCount, RIPPLE_MAX_SOURCES);
        for (uint j = 0u; j < n; ++j)
        {
            float4 a = gs_Source[j];
            // a hole over the inner 0.6 of the radius and a lifted rim out to 1.2: the wave runs out from the blast
            float v = a.w * Splash(distance(xz, a.xy), abs(a.z) * 0.6);
            push += v;
            bigPush += (a.z < 0.0) ? v : 0.0;
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
                    push += g_RainStrength * Splash(distance(xz, drop), g_RainRadius);
                }
            }
        }
        next -= float2(push, bigPush);
        cur -= float2(push, bigPush);
    }
    g_Out[p] = float4(next.x, cur.x, next.y, cur.y);
}
