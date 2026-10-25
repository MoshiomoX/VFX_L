// ============================================================
// WindSources.hlsli
// Local wind sources (2026-10-19, the user: "the wind itself should carry a
// wind coefficient and act on the world wind"). The sand tornadoes (and later
// anything else that blows) are written every frame by SwarmWindSourceCS into
// a fixed table of WIND_MAX_SOURCES entries; unused entries have radius 0.
// The grass (GrassVS), the ground debris (DebrisUpdateCS) and the particles
// (ParticleUpdateCS) add WindSourceAir() to the world wind, so one tornado
// bends the grass in a circle, lifts the sand and litter of the field and
// drags the smoke around it.
// 2026-10-20: a source can also push the air one way (pushDir x push,
// strongest at the centre, gone at the rim) - the gust spell is a vortex
// that blows forward. CPU sources (SwarmSystem::AddWindBurst) are appended
// to the table by the same CS.
// C++ mirror: Swarm::WindSource (48 bytes), Swarm::kMaxWindSources
// ============================================================
#ifndef WIND_SOURCES_HLSLI
#define WIND_SOURCES_HLSLI

#define WIND_MAX_SOURCES 16u

struct WindSource
{
    float3 pos;      // centre on the ground
    float radius;    // reach (m). 0 = empty slot
    float swirl;     // tangential speed at the core edge (m/s, counter-clockwise seen from above)
    float updraft;   // upward speed near the core (m/s)
    float inflow;    // speed toward the centre at the rim (m/s)
    float fade;      // 0..1 (birth / death)
    float2 pushDir;  // unit xz: the air is blown this way (gust). 0 = none
    float push;      // m/s at the centre
    float _pad;
};

// air velocity of all sources at a ground point xz (y = 0 plane). lift = strongest influence 0..1
float3 WindSourceAir(StructuredBuffer<WindSource> sources, float2 xz, out float lift)
{
    float3 air = 0;
    lift = 0.0;
    [loop]
    for (uint k = 0u; k < WIND_MAX_SOURCES; ++k)
    {
        WindSource w = sources[k];
        if (w.radius <= 0.0)
            continue;
        float2 d = xz - w.pos.xz;
        float dist = length(d);
        if (dist >= w.radius)
            continue;
        float r = dist / w.radius;
        float2 outw = dist > 1e-3 ? d / dist : float2(0, 1);
        float2 tang = float2(-outw.y, outw.x);
        // the swirl peaks around a third of the way out and fades to the rim; the updraft is a core thing
        float sw = saturate(r / 0.3) * saturate((1.0 - r) / 0.7);
        float up = saturate((1.0 - r) / 0.6);
        float inw = saturate((r - 0.25) / 0.75) * (1.0 - r);
        float ps = 1.0 - r * r;   // the push: full at the centre, gone at the rim
        air += float3(tang.x, 0.0, tang.y) * (w.swirl * sw * w.fade)
             + float3(0.0, w.updraft * up * w.fade, 0.0)
             - float3(outw.x, 0.0, outw.y) * (w.inflow * inw * w.fade)
             + float3(w.pushDir.x, 0.0, w.pushDir.y) * (w.push * ps * w.fade);
        lift = max(lift, (1.0 - r) * w.fade);
    }
    return air;
}

// the same air, plus the direction (unit xz) toward the core of the source that acts
// strongest here (2026-10-23: grass blades near a twister curl their tips toward it)
float3 WindSourceAirCore(StructuredBuffer<WindSource> sources, float2 xz, out float lift, out float2 toCore)
{
    float3 air = WindSourceAir(sources, xz, lift);
    toCore = float2(0.0, 0.0);
    float best = 0.0;
    [loop]
    for (uint k = 0u; k < WIND_MAX_SOURCES; ++k)
    {
        WindSource w = sources[k];
        float2 d = w.pos.xz - xz;
        float dist = length(d);
        float s = w.radius > 0.0 ? saturate(1.0 - dist / max(w.radius, 1e-3)) * w.fade : 0.0;
        if (s > best && dist > 1e-3)
        {
            best = s;
            toCore = d / dist;
        }
    }
    return air;
}

#endif
