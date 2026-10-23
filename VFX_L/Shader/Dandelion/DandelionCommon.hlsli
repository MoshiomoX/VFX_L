// ============================================================
// DandelionCommon.hlsli
// Dandelion clocks (World/DandelionField, 2026-10-22, the user: "add
// more things the wind moves"). On the grassland, patches of white
// seed heads on stems stand above the grass around the camera. A
// local wind source (the Twister, the vortex, the gust, a sand
// tornado), a strong weather wind or anything that tramples the grass
// (the player, an explosion, a beam, the gust's fan) blows a head
// apart: its seeds drift away on the wind and the head grows back.
//   DandelionCB  per-frame cbuffer, b0 in the CS / VS / PS
//   Flower       one stem + head, DandelionUpdateCS writes, DandelionVS reads
//   Seed         one flying seed, g_SeedsPer per flower
// C++ mirror: DandelionField::DandelionCB / Flower / Seed
//
// Placement: the same world-aligned lattice wrapped around the camera
// as the ground debris (Shader/Debris/DebrisCommon.hlsli).
// ============================================================
#ifndef DANDELION_COMMON_HLSLI
#define DANDELION_COMMON_HLSLI

#define DANDELION_HIDDEN 0x80000000u

cbuffer DandelionCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float3 g_CamPos;
    float g_Dt;               // seconds this step (0 while paused)
    float2 g_MapOrigin;       // world xz of the height map's min corner
    float2 g_MapSize;         // meters
    float2 g_WindDir;         // unit xz
    float g_WindSpeed;        // m/s (with the weather strength)
    float g_WindGust;
    float g_WindWaveScale;
    float g_WindWaveSpeed;
    float g_WindTime;         // the grass's wind clock
    float g_Window;           // half-size of the window, m
    uint g_Side;              // lattice cells per side
    float g_Spacing;          // lattice spacing, m
    uint g_SeedsPer;          // seeds per flower
    float g_Time;             // seconds, for the seeds' wander
    float g_Density;          // share of cells inside a patch that get a flower
    float g_PatchScale;       // m: size of the patches (value noise wavelength)
    float g_PatchCover;       // 0..1 share of the meadow covered by patches
    float g_FadeWidth;        // m: shrink to nothing over this width at the window edge
    float g_StemMin;          // stem height range, m
    float g_StemMax;
    float g_PuffRadius;       // seed head radius, m
    float g_SeedSize;         // flying seed half-size, m
    float g_ReleaseWind;      // weather wind (m/s) that starts blowing heads apart (each head x 0.8 .. 1.3)
    float g_ReleaseLocal;     // local wind source speed (m/s) at the head that blows it apart
    float g_ReleaseTrample;   // trample strength (0..1) that blows it apart
    float g_RegrowDelay;      // s before a bare stem starts to grow a new head
    float g_RegrowTime;       // s for the new head to grow to full
    float g_SeedDrag;         // 1/s: the seeds follow the air this fast
    float g_SeedGravity;      // m/s^2 (terminal fall = gravity / drag)
    float g_SeedLifeMin;      // s
    float g_SeedLifeMax;
    float g_SeedLift;         // local updraft x this for the seeds (light: the twister carries them up)
    float g_BendGain;         // stem bend per m/s of air
    float g_MaxSlope;         // steeper ground (rise / run) grows no flowers
    float3 g_StemColor;
    uint g_Seed;              // terrain seed
    float3 g_PuffColor;
    uint g_Reset;             // 1 = re-seat every flower
    float3 g_CamRight;
    float g_SizeMul;
    float3 g_CamUp;
    float _padCB;
    float3 g_PlayerPos;       // the player's body centre (feet + ~0.9 m)
    float g_PlayerRadius;     // heads closer than this to the player blow apart (0 = no player)
};

// 48 bytes
struct Flower
{
    float3 pos;     // stem base on the ground
    float puff;     // 0..1 how much of the head is there (1 = full)
    float2 bend;    // stem top offset per m of stem (smoothed toward the air)
    float regrow;   // s until a bare stem starts growing a new head
    uint cell;      // packed lattice cell (x | z << 16)
    uint seed;      // bits 0-23 variation, bit 31 = hidden
    float3 _pad;
};

// 32 bytes. life <= 0 = not flying
struct Seed
{
    float3 pos;
    float age;
    float3 vel;
    float life;
};

uint DandelionHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float DandelionRand(inout uint s)
{
    s = DandelionHash(s + 0x9e3779b9u);
    return (float) (s & 0xFFFFFFu) / 16777216.0;
}

float DandelionStemHeight(uint seed)
{
    return lerp(g_StemMin, g_StemMax, (float) ((seed >> 4) & 0xFFu) / 255.0) * g_SizeMul;
}

// world wind at xz (the GrassVS gust formula)
float2 DandelionWindAt(float2 xz)
{
    float phase = dot(xz, g_WindDir) * g_WindWaveScale * 6.2831853 - g_WindTime * g_WindWaveSpeed;
    float w = sin(phase) * 0.5 + 0.5;
    w *= w;
    return g_WindDir * (g_WindSpeed + g_WindGust * w);
}

#endif
