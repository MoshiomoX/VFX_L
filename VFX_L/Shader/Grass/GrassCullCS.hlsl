// ============================================================
// GrassCullCS.hlsl
// One thread per lattice cell in a square window around the camera.
// Each cell may grow one blade (hash-jittered inside the cell). Kept
// blades go to an append buffer that GrassVS draws with
// DrawInstancedIndirect.
//
// Dropped: beyond g_MaxDist, thinned out with distance (the kept ones
// get wider; the ones about to be thinned shrink first so nothing
// pops), no-grass cells (dirt ramps, the rim, unreachable plateaus:
// alpha of the ground texture), steep ground (cliff faces, from the
// height field), outside the view frustum.
//
// Since 2026-10-23 the kept blade is also animated here, once per blade
// instead of once per vertex (the blade went from 9 to 21 vertices):
//   world wind    two travelling gust waves (a big one and a smaller one
//                 from another direction). A strong gust turns the blade
//                 over: the lighter underside shows (sheen)
//   wind sources  (twisters, the vortex, sand tornadoes, gusts) lay the
//                 blade along the flow; where the air rises near the core
//                 the tip curls up and in toward it, twisted and shivering
//   trample       the map (player, enemies, blasts, beams, gusts and the
//                 swirl trail the wind sources leave behind)
//   fire          spent fuel shrinks and chars the blade, heat glows
// The spine is a cubic Bezier (root, c1, c2, tip). With no wind source
// around it is exactly the old curve: tip offset ~ t^2, height ~ t.
//
// 2026-10-25: at most g_MaxBlades blades are appended (the buffer's
// size). Each group counts its kept blades in shared memory, one thread
// reserves that many slots in g_BladeBudget, and only the blades inside
// the budget are appended. Before this the append counter (= the drawn
// instance count) kept going past the buffer with a fine spacing.
// ============================================================
#include "GrassCommon.hlsli"
#include "../Common/WindSources.hlsli"

Texture2D<float> g_Height : register(t0);  // GridWorld heights, texel centre = height cell centre
Texture2D<float4> g_Ground : register(t1); // rgb = ground color (linear), a = 1 grass / 0 none
Texture2D<float2> g_Trample : register(t2);
Texture2D<float2> g_Fire : register(t3);   // x = temperature, y = fuel left (0..1)
StructuredBuffer<WindSource> g_WindSources : register(t4);   // local wind (twisters, vortex, sand tornadoes, gusts)
SamplerState s_Linear : register(s0);      // clamp
AppendStructuredBuffer<GrassBlade> g_OutBlades : register(u0);
RWBuffer<uint> g_BladeBudget : register(u1);   // [0] = slots reserved so far this frame (cleared to 0 by the CPU)

groupshared uint gs_Kept;   // blades kept by this group
groupshared uint gs_Base;   // the group's first reserved slot

// 0..1 of one travelling wave: mostly calm with sharp crests
float GustWave(float2 xz, float2 dir, float perMeter, float speed)
{
    float g = sin(dot(xz, dir) * perMeter * 6.2831853 - g_Time * speed) * 0.5 + 0.5;
    return g * g;
}

// rough arc length of a cubic Bezier that starts at the origin
float BezierLength(float3 c1, float3 c2, float3 tip)
{
    float chord = length(tip);
    float net = length(c1) + length(c2 - c1) + length(tip - c2);
    return (chord + net) * 0.5;
}

// the blade's bend and look, once the cell is known to have a visible blade (b has pos / height / width /
// shade already)
void FinishBlade(float2 xz, float2 uv, float yaw, float curl, float rTwist, float rDry, float rPhase, inout GrassBlade b)
{
    float2 side = float2(cos(yaw), sin(yaw));
    float2 facing = float2(-side.y, side.x);

    // ---- fire: burnt grass is short and black, a hot cell glows at the tip ----
    b.burn = 0.0;
    b.glow = 0.0;
    if (g_FireOn > 0.5)
    {
        float2 fire = g_Fire.SampleLevel(s_Linear, uv, 0);
        b.burn = saturate(1.0 - fire.y);
        b.glow = saturate((fire.x - g_FireIgnite * 0.7) * 1.5);
        b.height *= 1.0 - g_FireShrink * b.burn;
    }
    float h = b.height;

    // ---- world wind: a big gust wave + a smaller one crossing it ----
    float g1 = GustWave(xz, g_WindDir, g_WindScale, g_WindSpeed);
    float g2 = GustWave(xz, g_Wave2.xy, g_Wave2.z, g_Wave2.w);
    float gust = g1 + g_BladeShape.z * g2 * (1.0 - g1);
    float flutter = sin(g_Time * 3.1 + yaw * 5.0) * 0.15 + sin(g_Time * 9.3 + rPhase * 6.2831853) * 0.06 * gust;
    float2 bend = facing * curl
        + g_WindDir * (g_WindStrength * (0.25 + gust + flutter))
        + g_Trample.SampleLevel(s_Linear, uv, 0) * g_TrampleBend;

    // ---- wind sources: laid along the flow, shivering across it; near a rising core the tip curls up ----
    float lift;
    float2 toCore;
    float3 air = WindSourceAirCore(g_WindSources, xz, lift, toCore);
    float spd = length(air.xz);
    float2 flow = spd > 1e-3 ? air.xz / spd : facing;
    float2 across = float2(-flow.y, flow.x);
    float lay = saturate(spd / max(g_Vortex.x, 0.1));
    float curlUp = saturate(air.y / max(g_Vortex.y, 0.1)) * lay;
    float shiver = sin(g_Time * (17.0 + 9.0 * rPhase) + rPhase * 40.0) * g_Vortex.z * saturate(spd / 10.0);
    bend += flow * (0.95 * lay) + across * shiver;

    float bl = length(bend);
    if (bl > 0.95)
    {
        bend *= 0.95 / bl;
        bl = 0.95;
    }
    float tipY = sqrt(max(1.0 - bl * bl, 0.0)) * h;

    // the old curve (offset ~ t^2, height ~ t) as a cubic Bezier
    float3 tipA = float3(bend.x * h, tipY, bend.y * h);
    float3 c1A = float3(0.0, tipY / 3.0, 0.0);
    float3 c2A = float3(tipA.x / 3.0, tipY * (2.0 / 3.0), tipA.z / 3.0);
    // curled: bent over along the flow at the root, then sucked up steeply toward the core
    // (reads as "pulled up by the twister" from the high camera), shivering across
    float3 f3 = float3(flow.x, 0.0, flow.y);
    float3 k3 = float3(toCore.x, 0.0, toCore.y);
    float3 a3 = float3(across.x, 0.0, across.y);
    float3 c1C = (f3 * 0.32 + float3(0.0, 0.22, 0.0)) * h;
    float3 c2C = (f3 * 0.45 + k3 * 0.12 + float3(0.0, 0.55, 0.0)) * h;
    float3 tipC = (f3 * 0.38 + k3 * 0.28 + float3(0.0, 0.85, 0.0) + a3 * (shiver * 2.0)) * h;
    float3 c1 = lerp(c1A, c1C, curlUp);
    float3 c2 = lerp(c2A, c2C, curlUp);
    float3 tip = lerp(tipA, tipC, curlUp);
    // length: the old curve keeps its own, the curled one is fitted to the blade's height
    float k = lerp(BezierLength(c1A, c2A, tipA), h * 1.02, curlUp) / max(BezierLength(c1, c2, tip), 1e-4);
    b.c1 = c1 * k;
    b.c2 = c2 * k;
    b.tip = tip * k;

    // ---- look ----
    // a strong gust turns the blades over (lighter underside, a band rolling across the field);
    // the wind sources' blades show it too
    b.sheen = smoothstep(0.45, 1.0, gust) * saturate(g_WindStrength / 0.35) * 0.8;
    b.sheen = max(b.sheen, max(curlUp, lay * 0.6));
    b.twist = (rTwist * 2.0 - 1.0) * g_BladeShape.x + curlUp * g_Vortex.w * (rTwist < 0.5 ? -1.0 : 1.0);
    b.dry = rDry < g_BladeShape.y ? 0.55 + 0.45 * (rDry / max(g_BladeShape.y, 1e-3)) : 0.0;
    b.side = side;
    b.pad = float2(0.0, 0.0);
}

// the blade of lattice cell id (false = no blade here: outside the window, thinned out, no grass, too
// steep, outside the view). One exit only: fxc warns X4000 at the call site for early returns
bool MakeBlade(uint3 id, out GrassBlade b)
{
    b = (GrassBlade) 0;
    bool ok = false;
    if (id.x < g_CellCount.x && id.y < g_CellCount.y)
    {
        int2 cell = g_CellMin + int2(id.xy);
        uint seed = GrassHash(asuint(cell.x) * 73856093u ^ GrassHash(asuint(cell.y) * 19349663u));
        float jx = GrassRand(seed);
        float jz = GrassRand(seed);
        float2 xz = (float2(cell) + float2(jx, jz)) * g_Spacing;

        float dist = distance(xz, g_CamPos.xz);
        float density = lerp(1.0, g_FarDensity, saturate((dist - g_FullDist) / max(g_MaxDist - g_FullDist, 1e-3)));
        float keep = GrassRand(seed);
        float2 uv = (xz - g_MapOrigin) / g_MapSize;
        if (dist <= g_MaxDist && keep <= density && all(uv > 0.0) && all(uv < 1.0))
        {
            uint tw, th;
            g_Ground.GetDimensions(tw, th);
            float4 ground = g_Ground.Load(int3(int2(uv * float2(tw, th)), 0));
            if (ground.a >= 0.5)
            {
                // steepness from the bilinear height 0.3m around (cliff edges are one height texel wide)
                float y = g_Height.SampleLevel(s_Linear, uv, 0);
                const float e = 0.3;
                float2 du = float2(e / g_MapSize.x, 0.0);
                float2 dv = float2(0.0, e / g_MapSize.y);
                float hx0 = g_Height.SampleLevel(s_Linear, uv - du, 0);
                float hx1 = g_Height.SampleLevel(s_Linear, uv + du, 0);
                float hz0 = g_Height.SampleLevel(s_Linear, uv - dv, 0);
                float hz1 = g_Height.SampleLevel(s_Linear, uv + dv, 0);
                float rise = max(max(abs(hx1 - y), abs(y - hx0)), max(abs(hz1 - y), abs(y - hz0)));
                if (rise <= g_MaxSlope * e)
                {
                    b.pos = float3(xz.x, y, xz.y);
                    b.height = lerp(g_HeightMin, g_HeightMax, GrassRand(seed)) * saturate((density - keep) / 0.08);
                    float yaw = GrassRand(seed) * 6.2831853;
                    b.width = lerp(g_WidthMin, g_WidthMax, GrassRand(seed)) * min(rsqrt(max(density, 0.05)), 3.0);
                    float curl = GrassRand(seed) * g_CurlMax;
                    b.shade = 0.85 + 0.3 * GrassRand(seed);
                    // drawn after the old ones, so every blade keeps the size / yaw / curl it had before 2026-10-23
                    float rTwist = GrassRand(seed);
                    float rDry = GrassRand(seed);
                    float rPhase = GrassRand(seed);

                    // a sphere around the blade (the tip bends at most one height sideways)
                    float3 c = b.pos + float3(0.0, b.height * 0.5, 0.0);
                    float r = b.height + b.width;
                    bool inView = true;
                    [unroll]
                    for (uint i = 0u; i < 6u; ++i)
                        inView = inView && dot(g_Frustum[i].xyz, c) + g_Frustum[i].w >= -r;
                    if (inView)
                    {
                        FinishBlade(xz, uv, yaw, curl, rTwist, rDry, rPhase, b);
                        ok = true;
                    }
                }
            }
        }
    }
    return ok;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    if (gi == 0u)
        gs_Kept = 0u;
    GroupMemoryBarrierWithGroupSync();

    GrassBlade b;
    bool kept = MakeBlade(id, b);
    uint local = 0u;   // this blade's place among the group's kept ones
    if (kept)
        InterlockedAdd(gs_Kept, 1u, local);
    GroupMemoryBarrierWithGroupSync();

    // one reservation per group (not one atomic per blade)
    if (gi == 0u)
    {
        uint base;
        InterlockedAdd(g_BladeBudget[0], gs_Kept, base);
        gs_Base = base;
    }
    GroupMemoryBarrierWithGroupSync();

    // the blades past the buffer's size are dropped, so the drawn count never exceeds it
    if (kept && gs_Base + local < g_MaxBlades)
        g_OutBlades.Append(b);
}
