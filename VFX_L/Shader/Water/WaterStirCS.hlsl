// ============================================================
// WaterStirCS.hlsl
// Enemies wading through the ponds (World/WaterSurface, 2026-10-28; the
// user: "add ripples when the monsters walk in the water"). One thread per
// enemy slot of the GPU swarm: a live enemy that stands in a pond and moves
// adds one push to a short list (g_Stirs); WaterRippleCS reads the list on
// its first step of the frame like the pushes of the areas. Like the
// player wading: a footstep push every g_Stride of walking plus a faint
// wake between them (grows with the speed and the time since the last
// pass), so an enemy standing still leaves the water calm.
// Skipped like the grass footprints (GrassStampCS): both final-swarm
// ghosts, the sand mage's tornado, a buried burrower / sand guardian, a
// vanished shadow swordsman, and anything whose feet are clearly above the
// ground under it (a gargoyle flying over the pond).
// The list holds at most g_StirMax pushes; past that the rest are dropped.
// ============================================================
// SwarmCommon's cbuffers are not used here; moved off b0 (WaterStirCB)
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

// Must match WaterSurface::StirCB
cbuffer WaterStirCB : register(b0)
{
    float2 g_StirOrigin;     // world xz of the map's min corner (= the water's map)
    float2 g_StirSize;       // meters
    float g_StirStrength;    // m pressed per (m/s of speed) per second
    float g_StirRadius;      // m, a normal mob
    float g_StirMinSpeed;    // m/s; slower = no ripple
    float g_StirDt;          // seconds since the last pass
    uint g_StirMax;          // capacity of g_Stirs
    uint g_HasHeight;        // 1 = g_Height is bound
    float g_StepStrength;    // m pressed by one footstep
    float g_Stride;          // m walked per footstep
    float g_StirTime;        // seconds (the water's clock, stops while paused)
    float g_StirGroundY;     // an enemy's position.y = its feet + this (the swarm's AICB groundY, 0.9)
    float2 _stirPad;
};

StructuredBuffer<SwarmEnemy> g_Enemies : register(t0);
Buffer<uint> g_EnemyStates : register(t1);         // 0 = dead
StructuredBuffer<SwarmEnemyExtra> g_EnemyExtra : register(t2);
Texture2D<float> g_Mask : register(t3);            // 1 = pond (the ripple field's texels)
Texture2D<float> g_Height : register(t4);          // GridWorld heights, texel = height cell (GrassRenderer::GetHeightSRV)
RWStructuredBuffer<float4> g_Stirs : register(u0); // x, z, radius, strength (m, + = pressed down in the middle)
RWBuffer<uint> g_StirCount : register(u1);         // [0] = pushes written this pass (cleared by the CPU)

// feet this far above the raw height cell under the centre = in the air (= GrassStampCS)
static const float STIR_AIR_HEIGHT = 0.8;
// a mob running faster than this pushes no harder (a charger's dash)
static const float STIR_MAX_SPEED = 8.0;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint j = id.x;
    uint count, stride;
    g_Enemies.GetDimensions(count, stride);
    if (j >= count || g_EnemyStates[j] == SWARM_DEAD)
        return;
    SwarmEnemyExtra ex = g_EnemyExtra[j];
    uint kind = ex.kind;
    if (SwarmIsGhost(kind) || kind == SWARM_KIND_TORNADO || SwarmBuried(ex)
        || (SwarmIsFixedElite(kind) && ex.fuse < -0.5))
        return;

    SwarmEnemy e = g_Enemies[j];
    float speed = length(e.velocity.xz);
    if (speed < g_StirMinSpeed)
        return;

    // in a pond?
    uint mw, mh;
    g_Mask.GetDimensions(mw, mh);
    int2 mc = int2(floor((e.position.xz - g_StirOrigin) / g_StirSize * float2(mw, mh)));
    if (any(mc < int2(0, 0)) || any(mc >= int2((int) mw, (int) mh)) || g_Mask.Load(int3(mc, 0)) < 0.5)
        return;

    // feet near the ground under it
    if (g_HasHeight != 0u)
    {
        uint hw, hh;
        g_Height.GetDimensions(hw, hh);
        int2 hc = int2(floor((e.position.xz - g_StirOrigin) / g_StirSize * float2(hw, hh)));
        hc = clamp(hc, int2(0, 0), int2((int) hw - 1, (int) hh - 1));
        // position.y is the capsule's centre: subtract groundY first (without it every enemy, 0.9 up, looked airborne)
        if (e.position.y - g_StirGroundY - g_Height.Load(int3(hc, 0)) > STIR_AIR_HEIGHT)
            return;
    }
    else if (kind == SWARM_KIND_GARGOYLE)
        return;   // cannot tell whether it flies

    // the big ones push a wider ring (= GrassStampCS's footprints)
    float r = g_StirRadius;
    if (kind == SWARM_KIND_BOSS) r *= 3.0;
    else if (SwarmIsBigElite(kind)) r *= 2.2;
    else if (kind == SWARM_KIND_BRUTE) r *= 1.3;

    // footsteps: one every g_Stride of walking, each enemy on its own beat (a hash of the slot). A push that
    // moves no water and slides along with the enemy mostly cancels itself, so the steady wake alone left only
    // a faint trace; the player's footsteps are what show (PlayerVFXSystem), so the enemies step too
    float rate = min(speed, STIR_MAX_SPEED) / max(g_Stride, 0.1);
    float beat = frac((float) (j * 2654435761u >> 8) / 16777216.0);
    bool stepped = floor(g_StirTime * rate + beat) != floor((g_StirTime - g_StirDt) * rate + beat);
    float s = stepped ? g_StepStrength : g_StirStrength * min(speed, STIR_MAX_SPEED) * g_StirDt;

    uint slot;
    InterlockedAdd(g_StirCount[0], 1u, slot);
    if (slot < g_StirMax)
        g_Stirs[slot] = float4(e.position.xz, r, s);
}
