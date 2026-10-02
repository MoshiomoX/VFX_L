// ============================================================
// LiquidCommon.hlsli
// Liquid entry (2026-10-02): a puddle of liquid lying on the ground,
// drawn the same way by both paths:
//   CPU : VFXLiquidEntry -> VFXLiquidRenderer -> VFXLiquidVS
//   GPU : an area whose recipe holds a Liquid entry -> SwarmLiquidVS
// and shaded by the one VFXLiquidPS. This file holds what both sides need:
// the look (LiquidDef), the VS -> PS struct, the droplet layout and the
// signed distance field of the puddle.
//
// Shape = a signed distance field (meters, < 0 inside). Each droplet is an
// ellipse SDF; they are merged with a polynomial smooth union, so droplets
// that come close fuse with a soft neck instead of overlapping as circles.
// Because the value is a distance, rim / depth / halo widths are given in
// meters and the edge is antialiased by fwidth(d).
//
// Nothing is simulated: where a droplet is at a given age is a closed-form
// curve of (seed, age, time left), so each pixel rebuilds the whole puddle.
//
// Local frame: x = along the throw (forward), y = to its left, meters from
// the puddle centre.
//
// C++ mirror: VFXLiquidDef (VFX_Editor/VFXLiquidDef.h), 160 bytes
// ============================================================
#ifndef LIQUID_COMMON_HLSLI
#define LIQUID_COMMON_HLSLI

struct LiquidDef
{
    float4 deepColor;   // linear rgb of deep liquid, a = its opacity
    float4 edgeColor;   // linear rgb at the rim, a = its opacity
    float4 glowColor;   // rgb = emissive, a = how much more the rim glows
    float4 skyColor;    // rgb = what it reflects at grazing angles, a = fresnel gain

    float radius;       // m. 0 = the area's radius (GPU path); the CPU path falls back to 2
    float fill;         // fraction of the radius the liquid may reach
    float spread;       // how much farther forward the splash reaches (0 = round)
    float wobble;       // droplet drift, fraction of the reach

    float blend;        // smooth-union distance, fraction of the reach
    float rimWidth;     // m: bright band just inside the edge
    float depthWidth;   // m: edge -> full depth (the surface curves over this band)
    float bump;         // height of that curve, m (normal tilt at the edge)

    float splashTime;   // s to fling the droplets out
    float dryTime;      // the last seconds it shrinks away
    float specGain;     // sun highlight
    float roughness;    // GGX roughness

    float bubbles;      // share of bubble cells holding a bubble (more where deep)
    float bubbleGain;   // bubble brightness
    float swirl;        // brightness / ripple of the slow surface noise
    float swirlScale;   // 1/m

    float lift;         // m above the ground (z-fighting)
    float cliff;        // m: ground this far above / below the centre is cut away
    float haloWidth;    // m: soft glow on the ground outside the edge
    float haloGain;

    uint lobes;         // lobes around the body (0..7)
    uint spatter;       // small drops flung forward (0..8)
    uint flags;         // reserved
    float pointGain;    // how much point lights (diffuse + highlight) act on the liquid
};

static const uint LIQUID_MAX_DROPS = 16u;   // 1 body + 7 lobes + 8 spatter

// VS -> PS (VFXLiquidVS and SwarmLiquidVS both write this)
struct LiquidVSOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float2 local : TEXCOORD1;                 // meters, x = forward, y = left
    float mask : TEXCOORD2;                   // 1 on the centre's ground, 0 where a cliff cuts it
    nointerpolation float4 info : TEXCOORD3;  // x = radius m, y = age s, z = time left s
    nointerpolation float4 frame : TEXCOORD4; // xy = forward (world xz), zw = left (world xz)
    nointerpolation uint2 ids : TEXCOORD5;    // x = LiquidDef index, y = seed
};

// ---- grid: a square of LIQUID_GRID x LIQUID_GRID quads, 6 vertices each ----
static const uint LIQUID_GRID = 16u;
static const uint LIQUID_VERTS = LIQUID_GRID * LIQUID_GRID * 6u;

// vertex id -> -1..1 across the square
float2 LiquidGridCorner(uint vid)
{
    static const float2 kCorner[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1)
    };
    uint q = vid / 6u;
    float2 cell = float2((float) (q % LIQUID_GRID), (float) (q / LIQUID_GRID));
    return (cell + kCorner[vid % 6u]) / (float) LIQUID_GRID * 2.0 - 1.0;
}

uint LiquidHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// 0..1, one value per (seed, droplet, which number)
float LiquidRand(uint seed, uint i, uint k)
{
    return (float) (LiquidHash(seed ^ (i * 0x9E3779B9u) ^ (k * 0x85EBCA6Bu)) & 0xFFFFFFu) / 16777216.0;
}

// forward / left in world xz. A zero direction picks an angle from the seed
void LiquidFrame(float2 dir, uint seed, out float2 fwd, out float2 left)
{
    float lenSq = dot(dir, dir);
    if (lenSq > 1e-6)
        fwd = dir * rsqrt(lenSq);
    else
    {
        float a = LiquidRand(seed, 99u, 0u) * 6.2831853;
        fwd = float2(cos(a), sin(a));
    }
    left = float2(-fwd.y, fwd.x);
}

float LiquidEaseOutBack(float t)
{
    const float c1 = 1.4;
    float u = t - 1.0;
    return 1.0 + (c1 + 1.0) * u * u * u + c1 * u * u;
}

struct LiquidDrop
{
    float2 c;       // centre, local m
    float2 axis;    // the way it was flung (unit, local)
    float r;        // half width, m
    float stretch;  // half length along axis / half width
};

// Droplet i of a puddle of radius R, age seconds old, left seconds to go.
//   i = 0 : the body       i = 1..lobes : lobes      then spatter
LiquidDrop LiquidDropAt(LiquidDef L, uint i, uint seed, float R, float age, float left, float time)
{
    float reach = R * L.fill;   // the liquid stays inside this circle
    float r0 = LiquidRand(seed, i, 0u);
    float r1 = LiquidRand(seed, i, 1u);
    float r2 = LiquidRand(seed, i, 2u);
    float r3 = LiquidRand(seed, i, 3u);
    bool spatter = i > L.lobes;

    float ang, dist, rad, stretch;
    if (i == 0u)
    {
        // the body: big, a little forward of the impact
        ang = 0.0;
        dist = 0.10 * reach;
        rad = 0.42 * reach;
        stretch = 1.0 + 0.25 * L.spread;
    }
    else if (!spatter)
    {
        // lobes all around; the ones thrown forward go farther, come out smaller and longer
        ang = (r0 * 2.0 - 1.0) * 3.14159265;
        float fwd = 0.5 + 0.5 * cos(ang);
        dist = reach * (0.22 + 0.28 * r1) * (1.0 + 0.8 * L.spread * fwd);
        rad = reach * (0.20 + 0.12 * r2) * (1.0 - 0.3 * fwd);
        stretch = 1.0 + 0.6 * L.spread * fwd;
    }
    else
    {
        // spatter: small drops flung forward in a cone of +-50 degrees
        ang = (r0 * 2.0 - 1.0) * 0.9;
        dist = reach * (0.62 + 0.30 * r1);
        rad = reach * (0.05 + 0.06 * r2);
        stretch = 1.0 + 1.2 * L.spread;
    }
    // keep the stretched droplet inside the reach
    dist = min(dist, max(reach - rad * stretch * 1.1, 0.0));
    float2 axis = float2(cos(ang), sin(ang));

    // ---- splash: fling out with a little overshoot, grow from a blob to full size ----
    float fling = LiquidEaseOutBack(saturate(age / max(L.splashTime, 1e-3)));
    float grow = 0.35 + 0.65 * saturate(age / max(L.splashTime * 1.4, 1e-3));
    // ---- settle: lobes pull back toward the body (surface tension), spatter thins out ----
    float settle = smoothstep(L.splashTime, L.splashTime + 1.0, age);
    if (i >= 1u && !spatter)
        dist *= lerp(1.0, 0.86, settle);
    if (spatter)
        rad *= lerp(1.0, 0.7, settle);
    // ---- alive: slow drift and breathing, each droplet on its own phase ----
    float ph = r3 * 6.2831853;
    float2 drift = float2(sin(time * 1.3 + ph), cos(time * 1.1 + ph * 1.7)) * (L.wobble * reach * settle);
    float breathe = 1.0 + 0.06 * sin(time * 2.1 + ph * 2.3);
    // ---- dry: shrink away over the last dryTime, the spatter first ----
    float dry = saturate(left / max(L.dryTime, 1e-3));
    float dryK = spatter ? dry * dry : sqrt(dry);

    LiquidDrop d;
    d.c = axis * (dist * fling) + drift;
    d.axis = axis;
    d.r = rad * grow * breathe * dryK;
    d.stretch = stretch;
    return d;
}

// Ellipse SDF (approximate, Inigo Quilez's bound k0 (k0 - 1) / k1) with
// semi-axes ab, q in the ellipse's own frame. g = unit normal of the level
// set through q (the gradient of (x/a)^2 + (y/b)^2 is 2 q / ab^2)
float LiquidEllipseSDF(float2 q, float2 ab, out float2 g)
{
    float2 qa = q / ab;
    float2 qb = q / (ab * ab);
    float k0 = length(qa);
    float k1 = max(length(qb), 1e-6);
    bool centre = k0 < 1e-4;   // the exact centre: no direction, the distance is the short half axis
    g = centre ? float2(0.0, 0.0) : qb / k1;
    return centre ? -min(ab.x, ab.y) : k0 * (k0 - 1.0) / k1;
}

// Polynomial smooth union (IQ): h = clamp(0.5 + 0.5 (b - a) / k),
// d = lerp(b, a, h) - k h (1 - h). Its exact partial derivatives are h and
// 1 - h, so the gradient blends with the same weight.
void LiquidSmoothUnion(inout float d, inout float2 g, float d2, float2 g2, float k)
{
    float h = saturate(0.5 + 0.5 * (d2 - d) / k);
    d = lerp(d2, d, h) - k * h * (1.0 - h);
    g = lerp(g2, g, h);
}

// Signed distance (m) from local point p to the puddle's edge, < 0 inside.
// grad = its gradient (local, roughly unit, points outward)
float LiquidSDF(LiquidDef L, float2 p, uint seed, float R, float age, float left, float time, out float2 grad)
{
    uint count = min(1u + min(L.lobes, 7u) + min(L.spatter, 8u), LIQUID_MAX_DROPS);
    float k = max(L.blend * R * L.fill, 1e-3);
    float d = 1e3;
    grad = float2(0.0, 0.0);

    [loop]
    for (uint i = 0u; i < count; ++i)
    {
        LiquidDrop dr = LiquidDropAt(L, i, seed, R, age, left, time);
        if (dr.r <= 1e-4)
            continue;
        float2 v = p - dr.c;
        float2 perp = float2(-dr.axis.y, dr.axis.x);
        float2 q = float2(dot(v, dr.axis), dot(v, perp));
        float2 gq;
        float di = LiquidEllipseSDF(q, float2(dr.r * dr.stretch, dr.r), gq);
        float2 gi = gq.x * dr.axis + gq.y * perp;   // back to the local frame
        LiquidSmoothUnion(d, grad, di, gi, k);
    }
    return d;
}

// Smooth value noise 0..1 (the slow swirl on the surface)
float LiquidNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    uint2 q = asuint(int2(i));
    float a = (float) (LiquidHash(q.x * 73856093u ^ q.y * 19349663u) & 0xFFFFu) / 65535.0;
    float b = (float) (LiquidHash((q.x + 1u) * 73856093u ^ q.y * 19349663u) & 0xFFFFu) / 65535.0;
    float c = (float) (LiquidHash(q.x * 73856093u ^ (q.y + 1u) * 19349663u) & 0xFFFFu) / 65535.0;
    float d = (float) (LiquidHash((q.x + 1u) * 73856093u ^ (q.y + 1u) * 19349663u) & 0xFFFFu) / 65535.0;
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// Bubbles: the puddle is cut into 0.32m cells; a cell holds one bubble with
// probability bubbles x deep. It swells over its own period and pops.
// Returns the brightness of its rim + a faint dome, 0 where there is none
float LiquidBubbles(LiquidDef L, float2 p, uint seed, float deep, float time)
{
    const float cell = 0.32;
    float2 g = floor(p / cell);
    float2 f = p / cell - g;
    uint2 q = asuint(int2(g));
    uint h = LiquidHash(seed ^ (q.x * 73856093u) ^ (q.y * 19349663u));
    float have = ((float) (h & 0xFFFFu) / 65535.0 <= L.bubbles * deep) ? 1.0 : 0.0;   // this cell holds one
    float u = (float) ((h >> 16) & 0xFFu) / 255.0;
    float w = (float) ((h >> 24) & 0xFFu) / 255.0;
    float2 c = 0.25 + 0.5 * float2(u, w);
    float period = 1.2 + 1.3 * u;
    float ph = frac(time / period + w);
    float rb = (0.08 + 0.12 * w) * smoothstep(0.0, 0.85, ph);   // radius in cell units
    float pop = 1.0 - smoothstep(0.85, 1.0, ph);
    float dist = length(f - c);
    float ring = 1.0 - smoothstep(0.0, 0.035, abs(dist - rb));
    float dome = (1.0 - smoothstep(0.0, max(rb, 1e-3), dist)) * 0.35;
    return (ring + dome) * pop * step(0.01, rb) * have;
}

// Shared by both vertex shaders: puts one grid vertex on the ground.
// xz = its world position, lp = the same point in the local frame,
// ground = the height at the puddle centre, heightAt = the terrain under xz
// (pass ground again where there is no terrain). Writes pos / world / local / mask
void LiquidPlaceVertex(LiquidDef L, float ground, float heightAt, float2 xz, float2 lp,
                       float4x4 viewProj, inout LiquidVSOut o)
{
    float dh = heightAt - ground;
    float cliff = max(L.cliff, 1e-3);
    // full on the centre's own ground, fading to 0 between cliff / 2 and cliff away from it
    o.mask = saturate(1.0 - (abs(dh) - cliff * 0.5) / (cliff * 0.5));
    float y = clamp(heightAt, ground - cliff, ground + cliff) + L.lift;
    o.world = float3(xz.x, y, xz.y);
    o.pos = mul(float4(o.world, 1.0), viewProj);
    o.local = lp;
}

#endif
