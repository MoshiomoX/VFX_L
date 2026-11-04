// ============================================================
// SwarmWindSourceCS.hlsl
// このフレームの局所風源を集める（2026-10-19）：生きている砂旋
// （SWARM_KIND_TORNADO）はそれぞれ WindSource 1 つになる（中心は足元、
// 届く距離 = 当たり半径 x g_RadiusMul、旋回 / 上昇 / 吸い込みは
// SwarmSystem::tornadoLook から、fade = 外殻と同じ伸び / 縮み）。
// 敵スロット 1 つに 1 スレッド。WIND_MAX_SOURCES 個のスロットに早い者勝ちで入れる
// （表とカウンタはディスパッチ前に CPU がゼロにする）。
// GrassVS / DebrisUpdateCS / ParticleUpdateCS が WindSources.hlsli 経由で読む。
// 2026-10-22：生きている吸い込みの範囲（渦、SWARM_AREA_EXTRA_PULL）も
// 風源になる（範囲スロット 1 つに 1 つ、先頭 g_AreaCount 本のスレッドで）：
// ほぼ吸い込みだけなので、草は内側へ傾き、落ち葉は中心へ吸われる。
// ============================================================
#define SWARM_FRAME_CB_REG b1
#include "../Common/SwarmCommon.hlsli"
#include "../Common/WindSources.hlsli"

// SwarmSystem::WindSourceCB（b0）と一致させること
cbuffer SwarmWindSourceCB : register(b0)
{
    float g_FootY;        // 敵の位置 -> 足元
    float g_RadiusMul;    // 届く距離 = 当たり半径 x これ
    float g_HitRadius;    // 砂旋の当たり半径（m）
    float g_Swirl;        // m/s
    float g_Updraft;      // m/s
    float g_Inflow;       // m/s
    float g_GrowTime;     // 伸びる / 縮む秒数（tornadoLook.growTime）
    uint g_SlotCount;     // kMaxEnemies
    uint g_CpuCount;      // cpuSources にある CPU の風源（SwarmSystem::AddWindBurst）の数。先に追加する（2026-10-20）
    uint g_TornadoOn;     // 0 = 砂旋を風源にしない（tornadoLook.windSource）
    uint g_VortexOn;      // 0 = 吸い込みの範囲を風源にしない（vortexWind.enabled）
    uint g_AreaCount;     // kMaxAreas
    float g_VRadiusMul;   // 届く距離 = 範囲の半径 x これ
    float g_VSwirl;       // m/s
    float g_VUpdraft;     // m/s
    float g_VInflow;      // m/s
    float g_VGroundY;     // 範囲の中心（弾の高さ）-> 地面
    float g_VFadeTime;    // 終わる前に弱まっていく秒数
    float2 _wsPad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t2);
StructuredBuffer<WindSource> cpuSources : register(t3);   // このフレームの CPU の風の噴き出し（fade は適用済み）
StructuredBuffer<SwarmArea> areas : register(t4);
Buffer<uint> areaStates : register(t5);

RWStructuredBuffer<WindSource> windSources : register(u0);
RWByteAddressBuffer windCount : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    // CPU の風源は先頭のスレッドに乗せる（1 本に 1 つ）
    if (i < g_CpuCount)
    {
        uint cslot;
        windCount.InterlockedAdd(0, 1u, cslot);
        if (cslot < WIND_MAX_SOURCES)
            windSources[cslot] = cpuSources[i];
    }
    // 吸い込みの範囲も先頭のスレッドに乗せる（範囲スロット 1 つに 1 つ）
    if (g_VortexOn != 0u && i < g_AreaCount && areaStates[i] != SWARM_DEAD)
    {
        SwarmArea a = areas[i];
        if ((a.tickNow & SWARM_AREA_EXTRA_PULL) != 0u && a.timeLeft > 0.0)
        {
            uint vslot;
            windCount.InterlockedAdd(0, 1u, vslot);
            if (vslot < WIND_MAX_SOURCES)
            {
                WindSource v;
                v.pos = a.center + float3(0.0, g_VGroundY, 0.0);
                v.radius = a.radius * g_VRadiusMul;
                v.swirl = g_VSwirl;
                v.updraft = g_VUpdraft;
                v.inflow = g_VInflow;
                v.fade = saturate(a.timeLeft / max(g_VFadeTime, 1e-3));
                v.pushDir = float2(0.0, 0.0);
                v.push = 0.0;
                v._pad = 0.0;
                windSources[vslot] = v;
            }
        }
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
