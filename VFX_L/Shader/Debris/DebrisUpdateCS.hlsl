// ============================================================
// DebrisUpdateCS.hlsl
// One step of the ground debris (World/GroundDebris, 2026-10-13).
// One thread per piece:
//   1) re-seat: the piece's lattice cell left the window (or g_Reset)
//      -> put it at rest on the ground of its new cell (hash of the
//      cell + terrain seed: same cell, same piece), hidden on cliffs
//      and where the density thins it out.
//   2) resting: the world wind at its spot (gust waves) above
//      g_LiftWind, a Gust fan or an explosion lifts it (stochastic for
//      the wind so a field does not jump at once).
//   3) airborne: gravity + relax toward the air velocity (world wind,
//      a Gust fan's stream, an explosion's outward blast) at g_Drag
//      -> leaves fall slowly and chase the wind. Tumbles while in
//      the air. Lands on the height map, one small bounce, then rests.
// ============================================================
#include "DebrisCommon.hlsli"
#include "../Common/WindSources.hlsli"

// = SwarmArea (Shader/Common/SwarmCommon.hlsli), 48 bytes. Only center / radius / flags are read
struct DebrisArea
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

Texture2D<float> g_Height : register(t0);          // GridWorld heights (GrassRenderer's texture)
StructuredBuffer<DebrisArea> g_Areas : register(t1);
Buffer<uint> g_AreaStates : register(t2);          // 0 = dead
StructuredBuffer<float4> g_AreaEnds : register(t3); // capsule (beam) end per slot, from AreaTickCS
StructuredBuffer<WindSource> g_WindSources : register(t4);   // local wind: the sand tornadoes (SwarmWindSourceCS, 2026-10-19)
SamplerState s_Linear : register(s0);
#define DEBRIS_AREA_CAPSULE 4u   // SWARM_AREA_CAPSULE: the beam. Its centre is the muzzle, the body is the segment
RWStructuredBuffer<Debris> g_Debris : register(u0);

float GroundAt(float2 xz)
{
    float2 uv = (xz - g_MapOrigin) / g_MapSize;
    return g_Height.SampleLevel(s_Linear, uv, 0);
}

// world wind at xz (the GrassVS gust formula, same clock as the blades and the particles)
float2 WindAt(float2 xz)
{
    float phase = dot(xz, g_WindDir) * g_WindWaveScale * 6.2831853 - g_WindTime * g_WindWaveSpeed;
    float w = sin(phase) * 0.5 + 0.5;
    w *= w;
    return g_WindDir * (g_WindSpeed + g_WindGust * w);
}

// air velocity inside the Gust fans / discs (0 outside). lift = 1 if any covers xz
float3 GustAir(float2 xz, out float lift)
{
    float3 air = 0;
    lift = 0.0;
    for (uint k = 0u; k < g_GustCount; ++k)
    {
        float2 root = g_GustA[k].xy, dir = g_GustA[k].zw;
        float len = g_GustB[k].x, radius = g_GustB[k].y, strength = g_GustB[k].z;
        float2 d = xz - root;
        if (len <= 0.0)
        {
            float dist = length(d);
            if (dist < radius)
            {
                float2 out2 = dist > 1e-3 ? d / dist : float2(0, 1);
                air += float3(out2.x, 0.45, out2.y) * (g_GustSpeed * strength * (1.0 - 0.5 * dist / radius));
                lift = 1.0;
            }
            continue;
        }
        float along = dot(d, dir);
        float2 sideV = d - dir * along;
        float side = length(sideV);
        float t = saturate(along / len);
        float r = radius * lerp(0.35, 1.0, t);
        if (along > -0.4 && along < len && side < r)
        {
            float2 flow = dir;
            if (side > 1e-3) flow = normalize(dir + sideV / side * (0.35 * side / r));   // spreads a little
            float s = strength * (1.0 - 0.6 * t) * (0.7 + 0.3 * (1.0 - side / r));       // strongest at the root, centre
            air += float3(flow.x, 0.4, flow.y) * (g_GustSpeed * s);
            lift = 1.0;
        }
    }
    return air;
}

// outward acceleration of the live explosion areas (Swarm::kAreaShake). lift = 1 inside one
float3 BlastAccel(float3 pos, out float lift)
{
    float3 acc = 0;
    lift = 0.0;
    for (uint a = 0u; a < DEBRIS_MAX_AREAS; ++a)
    {
        if (g_AreaStates[a] == 0u)
            continue;
        DebrisArea ar = g_Areas[a];
        if ((ar.flags & DEBRIS_AREA_SHAKE) == 0u)
            continue;
        // the beam (2026-10-13): pushed sideways off the segment, a little along it, over a band 2.5 x its
        // radius. Before this it was treated as a disc at its centre = the muzzle, so the leaves burst out
        // from the player's feet instead of along the beam
        if ((ar.flags & DEBRIS_AREA_CAPSULE) != 0u)
        {
            float2 seg = g_AreaEnds[a].xz - ar.center.xz;
            float segLen = length(seg);
            if (segLen < 1e-3)
                continue;
            float t = saturate(dot(pos.xz - ar.center.xz, seg) / (segLen * segLen));
            float2 d = pos.xz - (ar.center.xz + seg * t);
            float side = length(d);
            float r = ar.radius * 2.5;
            if (side > r || abs(pos.y - ar.center.y) > ar.halfHeight + 3.0)
                continue;
            float2 out2 = side > 1e-3 ? d / side : float2(seg.y, -seg.x) / segLen;
            float f = g_BlastAccel * 0.5 * (1.0 - side / r);
            acc += float3(out2.x * f, f * 0.4, out2.y * f) + float3(seg.x, 0.0, seg.y) / segLen * (f * 0.3);
            lift = 1.0;
            continue;
        }
        float r = ar.radius * 1.5;
        float2 d = pos.xz - ar.center.xz;
        float dist = length(d);
        if (dist > r || abs(pos.y - ar.center.y) > ar.halfHeight + 3.0)
            continue;
        float2 out2 = dist > 1e-3 ? d / dist : float2(0, 1);
        float f = g_BlastAccel * (1.0 - dist / r);
        acc += float3(out2.x * f, f * 0.55, out2.y * f);
        lift = 1.0;
    }
    return acc;
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_Side * g_Side)
        return;

    Debris d = g_Debris[i];

    // ---- 1) lattice cell of this piece inside the current window (torus wrap) ----
    int side = (int) g_Side;
    int ix = (int) (i % g_Side), iz = (int) (i / g_Side);
    int2 base = (int2) floor((g_CamPos.xz - g_Window) / g_Spacing);
    int kx = ix - base.x, kz = iz - base.y;
    int cx = base.x + (kx - (int) floor((float) kx / (float) side) * side);   // wrap into [base, base + side)
    int cz = base.y + (kz - (int) floor((float) kz / (float) side) * side);
    uint cell = ((uint) cx & 0xFFFFu) | (((uint) cz & 0xFFFFu) << 16);

    if (cell != d.cell || g_Reset != 0u)
    {
        uint s = DebrisHash((uint) cx * 73856093u ^ (uint) cz * 19349663u ^ g_Seed);
        float2 xz = (float2(cx, cz) + float2(DebrisRand(s), DebrisRand(s))) * g_Spacing;
        float2 uv = (xz - g_MapOrigin) / g_MapSize;
        uint tw, th;
        g_Height.GetDimensions(tw, th);
        float2 du = float2(1.0 / tw, 0), dv = float2(0, 1.0 / th);
        float y = g_Height.SampleLevel(s_Linear, uv, 0);
        float hx = g_Height.SampleLevel(s_Linear, uv + du, 0) - g_Height.SampleLevel(s_Linear, uv - du, 0);
        float hz = g_Height.SampleLevel(s_Linear, uv + dv, 0) - g_Height.SampleLevel(s_Linear, uv - dv, 0);
        float slope = length(float2(hx, hz)) / (2.0 * g_MapSize.x / tw);
        bool inside = all(uv > 0.0) && all(uv < 1.0);

        d.pos = float3(xz.x, y + 0.015, xz.y);
        d.restT = 10.0;
        d.vel = 0;
        d.cell = cell;
        d.rot = float3((DebrisRand(s) - 0.5) * 0.5, DebrisRand(s) * 6.2831853, (DebrisRand(s) - 0.5) * 0.5);
        d.air = 0.0;
        d.angVel = 0;
        // kind: 50% leaf, 35% clipping, 15% twig
        float kr = DebrisRand(s);
        uint kind = kr < 0.5 ? 0u : (kr < 0.85 ? 1u : 2u);
        d.seed = kind | ((s >> 8) & 0xFFFCu);
        if (!inside || slope > g_MaxSlope || DebrisRand(s) > g_Density)
            d.seed |= DEBRIS_HIDDEN;
        g_Debris[i] = d;
        return;
    }

    if ((d.seed & DEBRIS_HIDDEN) != 0u || g_Dt <= 0.0)
        return;

    uint rs = DebrisHash(d.seed ^ asuint(g_WindTime) ^ i * 2654435761u);
    float2 wind = WindAt(d.pos.xz);
    float gustLift, blastLift;
    float3 gustAir = GustAir(d.pos.xz, gustLift);
    float3 blast = BlastAccel(d.pos, blastLift);
    // local wind sources (the sand tornadoes): swirl around + updraft near the core
    float vortexLift;
    float3 vortexAir = WindSourceAir(g_WindSources, d.pos.xz, vortexLift);

    // ---- 2) resting: lift? ----
    if (d.air < 0.5)
    {
        d.restT += g_Dt;
        bool lift = false;
        float3 kick = 0;
        if (gustLift > 0.5)
        {
            lift = true;
            kick = gustAir * 0.35 + float3(0, 1.5 + 1.5 * DebrisRand(rs), 0);
        }
        else if (blastLift > 0.5)
        {
            lift = true;
            kick = blast * 0.08 + float3(0, 2.0 + 2.0 * DebrisRand(rs), 0);
        }
        else if (vortexLift > 0.12 && d.restT > 0.1)
        {
            // a tornado passing over: pieces peel off one after another (chance grows toward the core)
            if (DebrisRand(rs) < vortexLift * 4.0 * g_Dt)
            {
                lift = true;
                kick = vortexAir * 0.6 + float3(0, 0.8 + 1.2 * DebrisRand(rs), 0);
            }
        }
        else
        {
            // weather wind: a piece's own threshold (0.75 .. 1.25 x) and a chance per second so the field ripples
            float w = length(wind);
            float th = g_LiftWind * (0.75 + 0.5 * (float) ((d.seed >> 6) & 0xFFu) / 255.0);
            if (w > th && d.restT > 0.3)
            {
                float chance = saturate((w - th) / th) * 1.5 * g_Dt;
                if (DebrisRand(rs) < chance)
                {
                    lift = true;
                    kick = float3(wind.x, 0, wind.y) * 0.5 + float3(0, 0.5 + 0.9 * DebrisRand(rs), 0);
                }
            }
        }
        if (!lift)
        {
            // resting flutter in the wind: the card rocks a little (no position change)
            g_Debris[i] = d;
            return;
        }
        d.air = 1.0;
        d.restT = 0.0;
        d.vel = kick;
        float sp = max(length(kick), 1.0);
        d.angVel = float3(DebrisRand(rs) - 0.5, DebrisRand(rs) - 0.5, DebrisRand(rs) - 0.5) * (8.0 + 6.0 * sp);
        d.pos.y += 0.02;
    }

    // ---- 3) airborne ----
    d.restT += g_Dt;
    float3 air = float3(wind.x, 0, wind.y) + gustAir + vortexAir;
    float3 acc = float3(0, -g_Gravity, 0) + blast + (air - d.vel) * g_Drag;
    d.vel += acc * g_Dt;
    d.pos += d.vel * g_Dt;
    d.rot += d.angVel * g_Dt;
    d.angVel *= 1.0 - saturate(0.6 * g_Dt);

    float ground = GroundAt(d.pos.xz);
    if (d.pos.y <= ground + 0.015 && d.vel.y <= 0.0)
    {
        d.pos.y = ground + 0.015;
        float sp = length(d.vel);
        if (sp > 2.0 && gustLift < 0.5 && blastLift < 0.5 && vortexLift < 0.3)
        {
            // one small skip along the ground
            d.vel.y = -d.vel.y * 0.3;
            d.vel.xz *= 0.55;
            d.angVel *= 0.5;
        }
        else if (gustLift > 0.5 || blastLift > 0.5)
        {
            // still inside the stream: keep sliding along the ground
            d.vel.y = max(d.vel.y, 0.0);
        }
        else
        {
            d.vel = 0;
            d.air = 0.0;
            d.restT = 0.0;
            d.angVel = 0;
            // lie flat (keep the yaw)
            d.rot.x = (DebrisRand(rs) - 0.5) * 0.5;
            d.rot.z = (DebrisRand(rs) - 0.5) * 0.5;
        }
    }
    // gone off the map's edge: keep it at least on the ground
    d.pos.y = max(d.pos.y, ground + 0.015);
    g_Debris[i] = d;
}
