// ============================================================
// GrassFireCS.hlsl
// One step of the grass fire field (World/GrassFireField, 2026-10-12).
// One texel per small patch of the whole map: x = temperature, y = fuel
// left (1 = fresh grass, 0 = burnt). Ping-pong like the trample map.
//
// Per step:
//   heat is carried downwind (semi-Lagrangian: read the texel the wind
//   came from), spreads to the neighbours (diffusion) and cools.
//   A grass texel with fuel whose temperature passes g_Ignite burns:
//   it releases heat (keeps itself hot, lights the neighbours) and
//   uses up fuel. Cold burnt grass slowly regrows. Rain cools faster
//   and burns the fuel out quicker.
//   Heat sources = live GPU areas (explosions, meteor, fireball hit)
//   whose recipe has heat (g_RecipeHeat[vfxType] > 0, from the area
//   profile's "heat"), plus one CPU stamp (debug / tests). The group
//   compacts the live hot areas into shared memory first, so a texel
//   only loops over the few that matter (usually 0 to 5).
// Explicit diffusion is stable while g_Diffuse * dt <= 0.25 (texel units)
// ============================================================

// Must match GrassFireField::FireCB
cbuffer GrassFireCB : register(b0)
{
    float2 g_MapOrigin;     // world xz of the map's min corner
    float2 g_MapSize;       // meters
    float2 g_WindDir;       // unit xz, the way the wind blows
    float g_WindSpeed;      // m/s (with the weather strength)
    float g_Dt;             // seconds this step (0 while paused)
    float g_Ignite;         // temperature where grass starts to burn
    float g_Cool;           // 1/s
    float g_Diffuse;        // texel^2/s
    float g_Downwind;       // how much of the wind speed carries the heat (1 = full)
    float g_BurnRate;       // fuel per second while burning (1 / burn time)
    float g_Release;        // temperature per second a burning texel adds to itself
    float g_Regrow;         // fuel per second when cold
    float g_Rain;           // 0..1 rain (weather)
    float3 g_Stamp;         // CPU ignition: xz, radius (m)
    float g_StampHeat;      // 0 = no stamp
    float g_SourceMul;      // area radius x this = heated radius
    float g_Texel;          // meters per texel
    float g_RainCool;       // extra cooling at rain 1
    float _firePad;
};

// = SwarmArea (Shader/Common/SwarmCommon.hlsli), 48 bytes. Only center / radius / vfxType are read
struct FireArea
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

Texture2D<float2> g_In : register(t0);          // last step (temperature, fuel)
Texture2D<float4> g_Ground : register(t1);      // GrassRenderer's ground texture, a = grass grows here
StructuredBuffer<FireArea> g_Areas : register(t2);
Buffer<uint> g_AreaStates : register(t3);       // 0 = dead
StructuredBuffer<float> g_RecipeHeat : register(t4);   // per VFX recipe: ignition heat (0 = not a fire)
RWTexture2D<float2> g_Out : register(u0);

#define FIRE_MAX_AREAS 256u   // = SWARM_MAX_AREAS
#define FIRE_MAX_SOURCES 64u

groupshared float4 gs_Source[FIRE_MAX_SOURCES];   // xz, radius, heat
groupshared uint gs_SourceCount;

float LoadT(int2 p, int2 maxP)
{
    return g_In.Load(int3(clamp(p, int2(0, 0), maxP), 0)).x;
}

// bilinear temperature at a texel-space position (texel centres at .5)
float SampleT(float2 pos, int2 maxP)
{
    float2 q = pos - 0.5;
    int2 i = int2(floor(q));
    float2 f = q - float2(i);
    float a = LoadT(i, maxP), b = LoadT(i + int2(1, 0), maxP);
    float c = LoadT(i + int2(0, 1), maxP), d = LoadT(i + int2(1, 1), maxP);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    // ---- compact the live hot areas into shared memory (64 threads, 4 areas each) ----
    if (gi == 0u)
        gs_SourceCount = 0u;
    GroupMemoryBarrierWithGroupSync();

    uint heatCount, heatStride;
    g_RecipeHeat.GetDimensions(heatCount, heatStride);
    for (uint a = gi; a < FIRE_MAX_AREAS; a += 64u)
    {
        if (g_AreaStates[a] == 0u)
            continue;
        FireArea ar = g_Areas[a];
        float heat = (ar.vfxType < heatCount) ? g_RecipeHeat[ar.vfxType] : 0.0;
        if (heat <= 0.0)
            continue;
        uint slot;
        InterlockedAdd(gs_SourceCount, 1u, slot);
        if (slot < FIRE_MAX_SOURCES)
            gs_Source[slot] = float4(ar.center.xz, ar.radius * g_SourceMul, heat);
    }
    GroupMemoryBarrierWithGroupSync();

    uint w, h;
    g_Out.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;

    const int2 p = int2(id.xy);
    const int2 maxP = int2(w - 1, h - 1);
    const float2 c = g_In.Load(int3(p, 0));
    const float2 xz = g_MapOrigin + (float2(p) + 0.5) / float2(w, h) * g_MapSize;

    // grass here? (the ground texture has its own resolution)
    uint gw, gh;
    g_Ground.GetDimensions(gw, gh);
    const float2 uv = (float2(p) + 0.5) / float2(w, h);
    const bool grass = g_Ground.Load(int3(int2(uv * float2(gw, gh)), 0)).a > 0.5;

    // ---- heat: carried downwind, spread, cooled ----
    const float adv = min(g_Downwind * g_WindSpeed * g_Dt / max(g_Texel, 1e-3), 0.9);   // texels this step
    float T = SampleT(float2(p) + 0.5 - g_WindDir * adv, maxP);
    const float lap = LoadT(p + int2(1, 0), maxP) + LoadT(p - int2(1, 0), maxP)
                    + LoadT(p + int2(0, 1), maxP) + LoadT(p - int2(0, 1), maxP) - 4.0 * c.x;
    T += g_Diffuse * lap * g_Dt;
    T -= T * g_Cool * (1.0 + g_RainCool * g_Rain) * g_Dt;

    // ---- fuel: burns while hot, regrows when cold ----
    float fuel = c.y;
    const bool burning = grass && fuel > 0.001 && T > g_Ignite;
    if (burning)
    {
        T += g_Release * g_Dt;
        fuel -= g_BurnRate * (1.0 + 2.0 * g_Rain) * g_Dt;
    }
    else if (grass && T < g_Ignite * 0.5)
    {
        fuel += g_Regrow * g_Dt;
    }

    // ---- heat sources: explosions etc. (hot inside the radius, fading just outside) ----
    const uint n = min(gs_SourceCount, FIRE_MAX_SOURCES);
    for (uint i = 0u; i < n; ++i)
    {
        float4 s = gs_Source[i];
        float d = distance(xz, s.xy);
        if (d < s.z * 1.3)
            T = max(T, s.w * saturate(1.3 - d / max(s.z, 1e-3)));
    }
    if (g_StampHeat > 0.0)
    {
        float d = distance(xz, g_Stamp.xy);
        if (d < g_Stamp.z)
            T = max(T, g_StampHeat);
    }

    g_Out[p] = float2(clamp(T, 0.0, 2.0), saturate(fuel));
}
