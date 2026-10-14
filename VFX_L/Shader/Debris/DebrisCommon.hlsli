// ============================================================
// DebrisCommon.hlsli
// Ground debris (World/GroundDebris, 2026-10-13): light litter (leaves,
// grass clippings, twigs) lying on the ground around the camera. The
// wind field is invisible; what the player sees is what it moves, so
// the Gust spell, explosions and strong weather wind pick this up.
//   DebrisCB  per-frame cbuffer, b0 in the CS / VS / PS
//   Debris    one piece, DebrisUpdateCS writes, DebrisVS reads
// C++ mirror: GroundDebris::DebrisCB / Debris
//
// Placement: a world-aligned lattice (g_Spacing m) wrapped around the
// camera (g_Side x g_Side cells, torus): piece i owns lattice column
// i % side and row i / side; its world cell is the one of that
// column / row inside the current window. When the camera moves a
// row leaves the window on one side and re-seats on the other
// (at rest, on the ground, faded by distance so it never pops).
// ============================================================
#ifndef DEBRIS_COMMON_HLSLI
#define DEBRIS_COMMON_HLSLI

#define DEBRIS_MAX_GUSTS 4u
#define DEBRIS_MAX_AREAS 256u   // = SWARM_MAX_AREAS
#define DEBRIS_AREA_SHAKE 8u    // Swarm::kAreaShake: explosions
#define DEBRIS_HIDDEN 0x80000000u
#define DEBRIS_KIND_MASK 3u

cbuffer DebrisCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float3 g_CamPos;
    float g_Dt;             // seconds this step (0 while paused)
    float2 g_MapOrigin;     // world xz of the height map's min corner
    float2 g_MapSize;       // meters
    float2 g_WindDir;       // unit xz, the way the wind blows
    float g_WindSpeed;      // m/s (with the weather strength)
    float g_WindGust;       // m/s gust amplitude (same)
    float g_WindWaveScale;  // gust waves per meter
    float g_WindWaveSpeed;  // radians per second
    float g_WindTime;       // the grass's wind clock (same waves as the blades)
    float g_Window;         // half-size of the window, m (pieces live in cam +- this)
    uint g_Side;            // lattice cells per side of the window
    float g_Spacing;        // lattice spacing, m
    float g_LiftWind;       // resting pieces start tumbling when the wind at them passes this (m/s)
    float g_Gravity;        // m/s^2 (leaves fall slowly because of g_Drag)
    float g_Drag;           // 1/s: velocity relaxes to the air velocity at this rate
    float g_Density;        // 0..1 share of cells that get a piece
    float g_FadeWidth;      // m: pieces shrink to nothing over this width at the window edge
    float g_SizeMul;        // multiplies every piece's size
    float3 g_ColorLeaf;     // kind 0
    float g_BlastAccel;     // m/s^2 outward inside an explosion area (x (1 - d / r))
    float3 g_ColorClip;     // kind 1 (grass clipping)
    float g_GustSpeed;      // m/s air speed inside a Gust fan at strength 1
    float3 g_ColorTwig;     // kind 2
    float g_Time;           // seconds, for the resting flutter
    float4 g_GustA[DEBRIS_MAX_GUSTS];   // (root.x, root.z, dir.x, dir.z)
    float4 g_GustB[DEBRIS_MAX_GUSTS];   // (length, radius, strength 0..1, 0). length 0 = disc around the root
    uint g_GustCount;
    uint g_Seed;            // terrain seed: the same lattice cell always grows the same piece
    uint g_Reset;           // 1 = re-seat every piece (first frame / terrain rebuilt)
    float g_MaxSlope;       // steeper ground (rise / run) gets no debris (cliff faces)
};

// 64 bytes
struct Debris
{
    float3 pos;
    float restT;        // seconds at rest (>= 0). Airborne pieces count up too (time in the air)
    float3 vel;
    uint cell;          // packed lattice cell (x | z << 16), re-seat when it changes
    float3 rot;         // euler radians: x = pitch, y = yaw, z = roll
    float air;          // 1 = airborne, 0 = resting
    float3 angVel;      // radians per second (airborne tumble)
    uint seed;          // bits 0-1 kind, bits 2-15 size / colour variation, bit 31 = hidden (cliff / thinned)
};

uint DebrisHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float DebrisRand(inout uint s)
{
    s = DebrisHash(s + 0x9e3779b9u);
    return (float) (s & 0xFFFFFFu) / 16777216.0;
}

// card size (m) of a piece: x = along, y = across. kind 0 leaf, 1 clipping, 2 twig
float2 DebrisSize(uint seed)
{
    uint kind = seed & DEBRIS_KIND_MASK;
    float v = 0.8 + 0.4 * (float) ((seed >> 2) & 0xFu) / 15.0;   // 0.8 .. 1.2
    float2 s = float2(0.22, 0.12);                         // leaf
    if (kind == 1u) s = float2(0.26, 0.035);               // grass clipping
    else if (kind == 2u) s = float2(0.34, 0.03);           // twig
    return s * v * g_SizeMul;
}

// rotation matrix from the euler angles (yaw around Y, then pitch around the card's across axis, roll around along)
float3x3 DebrisRotation(float3 r)
{
    float cy = cos(r.y), sy = sin(r.y);
    float cp = cos(r.x), sp = sin(r.x);
    float cr = cos(r.z), sr = sin(r.z);
    float3x3 Ry = float3x3(cy, 0, sy, 0, 1, 0, -sy, 0, cy);
    float3x3 Rx = float3x3(1, 0, 0, 0, cp, -sp, 0, sp, cp);
    float3x3 Rz = float3x3(cr, -sr, 0, sr, cr, 0, 0, 0, 1);
    return mul(Ry, mul(Rx, Rz));
}

#endif
