// ============================================================
// DandelionUpdateCS.hlsl
// One step of the dandelion clocks (World/DandelionField, 2026-10-22).
// One thread per flower (it also moves its own g_SeedsPer seeds):
//   1) re-seat when its lattice cell left the window (or g_Reset):
//      a fresh full head on the ground of the new cell, hidden off the
//      grass, on cliffs and outside the meadow patches.
//   2) the stem bends toward the air at its head (world wind + local
//      wind sources).
//   3) a full head is blown apart by a local wind source, a strong
//      weather wind (stochastic, its own threshold) or trampling
//      (the grass trample map: the player, explosions, beams, the
//      gust's fan). Its seeds start at the head with the air's velocity.
//   4) a bare stem regrows a head after g_RegrowDelay.
//   5) seeds: relax toward the air (light: slow fall, carried up by an
//      updraft), wander a little, settle on the ground and fade.
// ============================================================
#include "DandelionCommon.hlsli"
#include "../Common/WindSources.hlsli"

Texture2D<float> g_Height : register(t0);          // GridWorld heights (GrassRenderer)
Texture2D<float4> g_Ground : register(t1);         // a = grows grass
Texture2D<float2> g_Trample : register(t2);        // grass trample bend (strength = length)
StructuredBuffer<WindSource> g_WindSources : register(t3);
SamplerState s_Linear : register(s0);
RWStructuredBuffer<Flower> g_Flowers : register(u0);
RWStructuredBuffer<Seed> g_Seeds : register(u1);

float2 MapUv(float2 xz)
{
    return (xz - g_MapOrigin) / g_MapSize;
}

float GroundAt(float2 xz)
{
    return g_Height.SampleLevel(s_Linear, MapUv(xz), 0);
}

// smooth value noise 0..1 (the meadow patches)
float PatchNoise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    uint s = g_Seed * 2654435761u;
    float a = (float) (DandelionHash((uint) (int) i.x * 73856093u ^ (uint) (int) i.y * 19349663u ^ s) & 0xFFFFu) / 65535.0;
    float b = (float) (DandelionHash((uint) (int) (i.x + 1.0) * 73856093u ^ (uint) (int) i.y * 19349663u ^ s) & 0xFFFFu) / 65535.0;
    float c = (float) (DandelionHash((uint) (int) i.x * 73856093u ^ (uint) (int) (i.y + 1.0) * 19349663u ^ s) & 0xFFFFu) / 65535.0;
    float d = (float) (DandelionHash((uint) (int) (i.x + 1.0) * 73856093u ^ (uint) (int) (i.y + 1.0) * 19349663u ^ s) & 0xFFFFu) / 65535.0;
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_Side * g_Side)
        return;

    Flower f = g_Flowers[i];
    uint seedBase = i * g_SeedsPer;

    // ---- 1) lattice cell inside the current window (torus wrap, same as the debris) ----
    int side = (int) g_Side;
    int ix = (int) (i % g_Side), iz = (int) (i / g_Side);
    int2 base = (int2) floor((g_CamPos.xz - g_Window) / g_Spacing);
    int kx = ix - base.x, kz = iz - base.y;
    int cx = base.x + (kx - (int) floor((float) kx / (float) side) * side);
    int cz = base.y + (kz - (int) floor((float) kz / (float) side) * side);
    uint cell = ((uint) cx & 0xFFFFu) | (((uint) cz & 0xFFFFu) << 16);

    if (cell != f.cell || g_Reset != 0u)
    {
        uint s = DandelionHash((uint) cx * 83492791u ^ (uint) cz * 2971215073u ^ g_Seed);
        float2 xz = (float2(cx, cz) + float2(DandelionRand(s), DandelionRand(s))) * g_Spacing;
        float2 uv = MapUv(xz);
        uint tw, th;
        g_Height.GetDimensions(tw, th);
        float2 du = float2(1.0 / tw, 0), dv = float2(0, 1.0 / th);
        float y = g_Height.SampleLevel(s_Linear, uv, 0);
        float hx = g_Height.SampleLevel(s_Linear, uv + du, 0) - g_Height.SampleLevel(s_Linear, uv - du, 0);
        float hz = g_Height.SampleLevel(s_Linear, uv + dv, 0) - g_Height.SampleLevel(s_Linear, uv - dv, 0);
        float slope = length(float2(hx, hz)) / (2.0 * g_MapSize.x / tw);
        bool inside = all(uv > 0.0) && all(uv < 1.0);
        float grass = g_Ground.SampleLevel(s_Linear, uv, 0).a;
        // patches: two octaves, the top g_PatchCover share of the noise is meadow
        float n = PatchNoise(xz / max(g_PatchScale, 1.0)) * 0.7 + PatchNoise(xz / max(g_PatchScale * 0.37, 1.0) + 17.3) * 0.3;
        bool inPatch = n > 1.0 - g_PatchCover;

        f.pos = float3(xz.x, y, xz.y);
        f.puff = 1.0;
        f.bend = 0;
        f.regrow = 0.0;
        f.cell = cell;
        f.seed = s & 0xFFFFFFu;
        f._pad = 0;
        if (!inside || slope > g_MaxSlope || grass < 0.5 || !inPatch || DandelionRand(s) > g_Density)
            f.seed |= DANDELION_HIDDEN;
        g_Flowers[i] = f;
        for (uint k = 0u; k < g_SeedsPer; ++k)
        {
            Seed z = (Seed) 0;
            g_Seeds[seedBase + k] = z;
        }
        return;
    }

    if ((f.seed & DANDELION_HIDDEN) != 0u || g_Dt <= 0.0)
        return;

    uint rs = DandelionHash(f.seed ^ asuint(g_Time) ^ i * 2654435761u);

    // ---- 2) the air at the head: world wind + local sources ----
    float stem = DandelionStemHeight(f.seed);
    float2 wind = DandelionWindAt(f.pos.xz);
    float lift;
    float3 local = WindSourceAir(g_WindSources, f.pos.xz, lift);
    float2 airXZ = wind + local.xz;
    float2 bendGoal = airXZ * g_BendGain;
    float bl = length(bendGoal);
    if (bl > 0.6)
        bendGoal *= 0.6 / bl;   // lie at most ~30 degrees over
    f.bend = lerp(f.bend, bendGoal, saturate(g_Dt * 6.0));

    // ---- 3) blow the head apart ----
    if (f.puff > 0.95)
    {
        bool release = false;
        float2 trample = g_Trample.SampleLevel(s_Linear, MapUv(f.pos.xz), 0);
        float localSpeed = length(local);
        // the player brushing past: a wider circle than the grass's trample disc, so running through a patch clears a swath
        float2 fromPlayer = f.pos.xz - g_PlayerPos.xz;
        float playerDist = length(fromPlayer);
        if (g_PlayerRadius > 0.0 && playerDist < g_PlayerRadius && abs(f.pos.y - g_PlayerPos.y) < 2.0)
        {
            release = true;
            trample = (playerDist > 1e-3 ? fromPlayer / playerDist : float2(0.0, 1.0)) * 0.8;
        }
        else if (length(trample) > g_ReleaseTrample)
            release = true;
        else if (localSpeed > g_ReleaseLocal && DandelionRand(rs) < saturate(localSpeed / g_ReleaseLocal - 0.6) * 8.0 * g_Dt)
            release = true;   // a source passing over: heads go one after another as it closes in
        else
        {
            float w = length(wind);
            float thr = g_ReleaseWind * (0.8 + 0.5 * (float) ((f.seed >> 12) & 0xFFu) / 255.0);
            if (w > thr && DandelionRand(rs) < saturate((w - thr) / thr) * 0.8 * g_Dt)
                release = true;
        }
        if (release)
        {
            f.puff = 0.0;
            f.regrow = g_RegrowDelay * (0.7 + 0.6 * DandelionRand(rs));
            float3 top = f.pos + float3(f.bend.x * stem, stem, f.bend.y * stem);
            float3 air = float3(airXZ.x, local.y, airXZ.y) + float3(trample.x, 0.0, trample.y) * 3.0;
            for (uint k = 0u; k < g_SeedsPer; ++k)
            {
                Seed z;
                float a = DandelionRand(rs) * 6.2831853, c = DandelionRand(rs) * 2.0 - 1.0;
                float3 dir = float3(sqrt(1.0 - c * c) * cos(a), c, sqrt(1.0 - c * c) * sin(a));
                z.pos = top + dir * (g_PuffRadius * g_SizeMul * 0.8);
                z.vel = air * (0.5 + 0.5 * DandelionRand(rs)) + dir * 0.7 + float3(0.0, 0.5 + 0.7 * DandelionRand(rs), 0.0);
                z.age = 0.0;
                z.life = lerp(g_SeedLifeMin, g_SeedLifeMax, DandelionRand(rs));
                g_Seeds[seedBase + k] = z;
            }
        }
    }
    // ---- 4) regrow ----
    else
    {
        f.regrow -= g_Dt;
        if (f.regrow <= 0.0)
            f.puff = min(1.0, f.puff + g_Dt / max(g_RegrowTime, 0.1));
    }
    g_Flowers[i] = f;

    // ---- 5) the seeds ----
    for (uint k = 0u; k < g_SeedsPer; ++k)
    {
        Seed z = g_Seeds[seedBase + k];
        if (z.life <= 0.0)
            continue;
        z.age += g_Dt;
        if (z.age >= z.life)
        {
            z.life = 0.0;
            g_Seeds[seedBase + k] = z;
            continue;
        }
        float ground = GroundAt(z.pos.xz);
        float above = z.pos.y - ground;
        float sl;
        float3 la = WindSourceAir(g_WindSources, z.pos.xz, sl);
        la *= saturate(1.0 - (above - 4.0) / 4.0);   // the sources fade out above ~8 m (no seeds to the sky)
        la.y *= g_SeedLift;
        float2 w2 = DandelionWindAt(z.pos.xz);
        // a little wander so the cloud spreads out instead of travelling as one blob
        float ph = z.age * 1.7 + (float) (k * 7u + i) * 0.37;
        float3 wander = float3(sin(ph * 1.3), sin(ph * 0.9 + 1.7) * 0.5, cos(ph * 1.1)) * 0.6;
        float3 air = float3(w2.x, 0.0, w2.y) + la + wander;
        float3 acc = (air - z.vel) * g_SeedDrag + float3(0.0, -g_SeedGravity, 0.0);
        z.vel += acc * g_Dt;
        z.pos += z.vel * g_Dt;
        if (z.pos.y < ground + 0.03)
        {
            // landed: stays put and fades out over the next second
            z.pos.y = ground + 0.03;
            z.vel = 0;
            z.age = max(z.age, z.life - 1.0);
        }
        g_Seeds[seedBase + k] = z;
    }
}
