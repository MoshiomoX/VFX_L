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
// ============================================================
#include "GrassCommon.hlsli"

// = SwarmEnemy (Shader/Common/SwarmCommon.hlsli), 48 bytes. Only position is read
struct StampEnemy
{
    float3 position;
    uint hp;
    float3 velocity;
    float moveSpeed;
    float yaw;
    float animTime;
    uint animIndex;
    float attackCooldown;
};
// = SwarmEnemyExtra, 8 bytes
struct StampExtra
{
    uint kind;
    float fuse;
};
#define STAMP_KIND_ELITE 2u
#define STAMP_KIND_BOSS 3u
#define STAMP_KIND_GHOST 4u
#define STAMP_KIND_DARKGHOST 14u
#define STAMP_KIND_BRUTE 7u

StructuredBuffer<StampEnemy> g_Enemies : register(t0);
Buffer<uint> g_EnemyStates : register(t1);         // 0 = dead
StructuredBuffer<StampExtra> g_EnemyExtra : register(t2);
RWTexture2D<float2> g_TrampleOut : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint j = id.x;
    uint count, stride;
    g_Enemies.GetDimensions(count, stride);
    if (j >= count || g_TrampleExtra.z <= 0.0)
        return;
    if (g_EnemyStates[j] == 0u)
        return;
    uint kind = g_EnemyExtra[j].kind;
    if (kind == STAMP_KIND_GHOST || kind == STAMP_KIND_DARKGHOST)   // floats above the grass
        return;

    // footprint: the body radius by kind (the big ones press a wider disc)
    float r = g_TrampleExtra.w;
    if (kind == STAMP_KIND_BOSS) r *= 3.0;
    else if (kind == STAMP_KIND_ELITE) r *= 2.2;
    else if (kind == STAMP_KIND_BRUTE) r *= 1.3;

    uint w, h;
    g_TrampleOut.GetDimensions(w, h);
    float2 texel = g_MapSize / float2(w, h);
    float2 c = g_Enemies[j].position.xz;
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
