// ============================================================
// SwarmWindSourceCS.hlsl
// Collects the local wind sources of this frame (2026-10-19): every live sand
// tornado (SWARM_KIND_TORNADO) becomes one WindSource (centre at its feet,
// reach = hit radius x g_RadiusMul, swirl / updraft / inflow from
// SwarmSystem::tornadoLook, fade = the same grow / shrink as its shell).
// One thread per enemy slot, first come first served into WIND_MAX_SOURCES
// slots (the table and the counter are zeroed by the CPU before the dispatch).
// Read by GrassVS / DebrisUpdateCS / ParticleUpdateCS through WindSources.hlsli.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#include "../Common/SwarmCommon.hlsli"
#include "../Common/WindSources.hlsli"

// Must match SwarmSystem::WindSourceCB (b0)
cbuffer SwarmWindSourceCB : register(b0)
{
    float g_FootY;        // enemy position -> feet
    float g_RadiusMul;    // reach = hit radius x this
    float g_HitRadius;    // the tornado's hit radius (m)
    float g_Swirl;        // m/s
    float g_Updraft;      // m/s
    float g_Inflow;       // m/s
    float g_GrowTime;     // seconds to grow / shrink (tornadoLook.growTime)
    uint g_SlotCount;     // kMaxEnemies
    uint g_CpuCount;      // CPU sources (SwarmSystem::AddWindBurst) in cpuSources, appended first (2026-10-20)
    uint g_TornadoOn;     // 0 = the tornadoes are not wind sources (tornadoLook.windSource)
    float2 _wsPad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t2);
StructuredBuffer<WindSource> cpuSources : register(t3);   // the CPU bursts of this frame (fade already applied)

RWStructuredBuffer<WindSource> windSources : register(u0);
RWByteAddressBuffer windCount : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    // the CPU sources ride on the first threads (one each)
    if (i < g_CpuCount)
    {
        uint cslot;
        windCount.InterlockedAdd(0, 1u, cslot);
        if (cslot < WIND_MAX_SOURCES)
            windSources[cslot] = cpuSources[i];
    }
    if (i >= g_SlotCount || g_TornadoOn == 0u)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    SwarmEnemyExtra ex = enemyExtra[i];
    if (ex.kind != SWARM_KIND_TORNADO || ex.fuse <= 0.0)
        return;

    uint slot;
    windCount.InterlockedAdd(0, 1u, slot);
    if (slot >= WIND_MAX_SOURCES)
        return;

    SwarmEnemy e = enemies[i];
    float grow = max(g_GrowTime, 1e-3);
    float fade = saturate(e.animTime / grow) * saturate(ex.fuse / grow);

    WindSource w;
    w.pos = e.position + float3(0.0, g_FootY, 0.0);
    w.radius = g_HitRadius * g_RadiusMul;
    w.swirl = g_Swirl;
    w.updraft = g_Updraft;
    w.inflow = g_Inflow;
    w.fade = fade;
    w.pushDir = float2(0.0, 0.0);
    w.push = 0.0;
    w._pad = 0.0;
    windSources[slot] = w;
}
