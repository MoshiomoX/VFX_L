// ============================================================
// GrassStampCS.hlsl
// Enemies' feet on the trample map (2026-10-13). One thread per enemy
// slot of the GPU swarm: a live, grounded enemy stamps a small disc
// (pushed outward from its centre) into the texels under it. Runs
// right after GrassTrampleCS on the same output texture: a scatter
// write (no read-back of the texel, typed UAV loads of R16G16_FLOAT
// are not guaranteed), so where an enemy stands its footprint simply
// replaces whatever the gather pass wrote there. With hundreds of
// enemies walking through the grass this is what makes the field
// look walked on.
// 2026-10-25: the kinds come from SwarmCommon.hlsli (the old local table
// missed the stage's fixed elites and the kinds that are not on the
// grass). No footprint for: both final-swarm ghosts (they float), the
// sand mage's tornado (its wind source already presses the grass), a
// buried burrower / sand guardian, a vanished shadow swordsman, and
// anything whose feet are clearly above the ground under it (a flying or
// diving gargoyle, a mob dropping off a terrace).
// ============================================================
// SwarmCommon's cbuffers are not used here; moved off b0 (GrassCB)
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"
#include "GrassCommon.hlsli"

StructuredBuffer<SwarmEnemy> g_Enemies : register(t0);
Buffer<uint> g_EnemyStates : register(t1);         // 0 = dead
StructuredBuffer<SwarmEnemyExtra> g_EnemyExtra : register(t2);
Texture2D<float> g_Height : register(t3);          // GridWorld heights, texel = height cell (GrassCullCS reads the same)
RWTexture2D<float2> g_TrampleOut : register(u0);

// feet this far above the raw height cell under the centre = in the air (no footprint). A terrace step is
// 2.5m, an enemy standing on its edge is level with the raw cell, a flying gargoyle is g_GargoyleHover up
static const float STAMP_AIR_HEIGHT = 0.8;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint j = id.x;
    uint count, stride;
    g_Enemies.GetDimensions(count, stride);
    if (j >= count || g_TrampleExtra.z <= 0.0)
        return;
    if (g_EnemyStates[j] == SWARM_DEAD)
        return;
    SwarmEnemyExtra ex = g_EnemyExtra[j];
    uint kind = ex.kind;
    if (SwarmIsGhost(kind) || kind == SWARM_KIND_TORNADO || SwarmBuried(ex)
        || (SwarmIsFixedElite(kind) && ex.fuse < -0.5))   // the shadow swordsman vanished (= SwarmUntargetable)
        return;

    float3 p = g_Enemies[j].position;
    uint hw, hh;
    g_Height.GetDimensions(hw, hh);
    int2 hc = int2(floor((p.xz - g_MapOrigin) / g_MapSize * float2(hw, hh)));
    hc = clamp(hc, int2(0, 0), int2((int) hw - 1, (int) hh - 1));
    if (p.y - g_Height.Load(int3(hc, 0)) > STAMP_AIR_HEIGHT)
        return;

    // footprint: the body radius by kind (the big ones press a wider disc)
    float r = g_TrampleExtra.w;
    if (kind == SWARM_KIND_BOSS) r *= 3.0;
    else if (SwarmIsBigElite(kind)) r *= 2.2;   // the elite and the stage's fixed elites
    else if (kind == SWARM_KIND_BRUTE) r *= 1.3;

    uint w, h;
    g_TrampleOut.GetDimensions(w, h);
    float2 texel = g_MapSize / float2(w, h);
    float2 c = p.xz;
    float2 cuv = (c - g_MapOrigin) / texel;        // texel space
    int2 lo = int2(floor(cuv - r / texel));
    int2 hi = int2(floor(cuv + r / texel));
    lo = max(lo, int2(0, 0));
    hi = min(hi, int2((int) w - 1, (int) h - 1));
    if (hi.x - lo.x > 12 || hi.y - lo.y > 12)      // safety: a boss is at most ~7 texels across
        return;

    for (int y = lo.y; y <= hi.y; ++y)
    {
        for (int x = lo.x; x <= hi.x; ++x)
        {
            float2 xz = g_MapOrigin + (float2(x, y) + 0.5) * texel;
            float2 d = xz - c;
            float dist = length(d);
            if (dist >= r)
                continue;
            float s = g_TrampleExtra.z * sqrt(1.0 - dist / r);
            float2 push = (dist > 1e-3) ? d / dist * s : float2(0.0, 0.0);
            g_TrampleOut[int2(x, y)] = push;
        }
    }
}
