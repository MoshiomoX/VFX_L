// ============================================================
// TreeSwayCS.hlsl
// 木 1 本に 1 スレッド：樹冠の高さの空気の速さから、梢の傾きをばねで動かす。
//   空気 = 天気の風（草と同じ波）+ 風源（竜巻・砂旋・突風の魔法）を風の格子で置き換えた物 + 突風の扇
//   目標の傾き = 風速² × flex（上限あり）、ばね（固有振動数は背の高い木ほど低い）で追う
//   爆発（kAreaShake の範囲）は外向きに押し、揺さぶり shake を足す（だんだん消える）
// ============================================================
#define TREE_SWAY_CS
#include "TreeCommon.hlsli"
#include "../Common/WindSources.hlsli"
#include "../Common/WindGrid.hlsli"
#include "../Common/SwarmCore.hlsli"
#include "../Common/SwarmAreaTypes.hlsli"
#include "../Common/MathConst.hlsli"

#define TREE_MAX_GUSTS 4u

cbuffer SwayCB : register(b0)
{
    float g_Dt;               // このステップの秒（ポーズ中は 0）
    float g_Time;
    uint g_TreeCount;
    uint g_GustCount;
    float2 g_WindDir;         // 天気の風（草と同じ）
    float g_WindSpeed;
    float g_WindGust;
    float g_WindWaveScale;
    float g_WindWaveSpeed;
    float g_WindTime;
    float g_GustSpeed;        // 強さ 1 の突風の扇の中の風速 m/s
    float g_LeanPerWind2;     // 風速² 1 あたりの梢の傾き（高さに対する割合）
    float g_MaxLean;          // 梢の傾きの上限（高さに対する割合）
    float g_Freq;             // 高さ 5m の木の固有振動数 Hz
    float g_Damping;          // 減衰比
    float g_WindRef;          // windAmt = 1 になる風速 m/s
    float g_Turbulence;       // 乱れ（風速に比例した目標のゆらぎ）
    float g_BlastAccel;       // 爆発の範囲の中の外向きの加速度 m/s²
    float g_ShakeDecay;       // 揺さぶりの消える速さ 1/s
    float4 g_GustA[TREE_MAX_GUSTS];   // (root.x, root.z, dir.x, dir.z)
    float4 g_GustB[TREE_MAX_GUSTS];   // (length, radius, strength 0..1, 0)。length 0 = 根元の周りの円
};

StructuredBuffer<SwarmArea> g_Areas : register(t0);   // 読むのは center / radius / flags だけ
Buffer<uint> g_AreaStates : register(t1);          // 0 = 死んでいる
StructuredBuffer<WindSource> g_WindSources : register(t2);
RWStructuredBuffer<TreeState> g_States : register(u0);

float2 WeatherWind(float2 xz)
{
    float phase = dot(xz, g_WindDir) * g_WindWaveScale * TWO_PI - g_WindTime * g_WindWaveSpeed;
    float w = sin(phase) * 0.5 + 0.5;
    w *= w;
    return g_WindDir * (g_WindSpeed + g_WindGust * w);
}

// 突風の扇 / 円盤の中の空気（GroundDebris と同じ形）
float2 GustAir(float2 xz)
{
    float2 air = 0;
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
                air += out2 * (g_GustSpeed * strength * (1.0 - 0.5 * dist / radius));
            }
            continue;
        }
        float along = dot(d, dir);
        float side = length(d - dir * along);
        float t = saturate(along / len);
        float r = radius * lerp(0.35, 1.0, t);
        if (along > -0.4 && along < len && side < r)
            air += dir * (g_GustSpeed * strength * (1.0 - 0.6 * t));
    }
    return air;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_TreeCount)
        return;
    TreeState s = g_States[i];
    float dt = g_Dt;
    if (dt <= 0.0)
        return;

    float2 xz = s.crown.xz;
    float lift;
    float3 local = WindSourceAir(g_WindSources, xz, lift);
    local = WindGridBlend(local, s.crown);
    float2 air = WeatherWind(xz) + local.xz + GustAir(xz);
    float speed = length(air);

    // 爆発：樹冠を外へ押し、揺さぶる
    float2 blast = 0;
    for (uint a = 0u; a < SWARM_MAX_AREAS; ++a)
    {
        if (g_AreaStates[a] == 0u)
            continue;
        SwarmArea ar = g_Areas[a];
        if ((ar.flags & SWARM_AREA_SHAKE) == 0u || (ar.flags & SWARM_AREA_CAPSULE) != 0u)
            continue;
        float2 d = xz - ar.center.xz;
        float reach = ar.radius * 2.5 + s.height * 0.3;
        float dist = length(d);
        if (dist >= reach)
            continue;
        float k = 1.0 - dist / reach;
        blast += (dist > 1e-3 ? d / dist : float2(1, 0)) * (g_BlastAccel * k);
        s.shake = max(s.shake, k);
    }
    s.shake *= exp(-g_ShakeDecay * dt);

    // 目標の傾き：風速² に比例（上限あり）+ 風速に比例した乱れ
    float h = max(s.height, 0.3);
    float2 target = air * (speed * g_LeanPerWind2 * s.flex * h);
    float maxLean = g_MaxLean * h * s.flex;
    float tl = length(target);
    if (tl > maxLean)
        target *= maxLean / tl;
    float ph = s.root.x * 0.37 + s.root.z * 0.61;
    target += float2(sin(g_Time * 1.3 + ph), cos(g_Time * 1.07 + ph * 1.7)) * (speed * g_Turbulence * s.flex * h * 0.01);

    // ばね（背の高い木ほどゆっくり）。dt が大きい時は分けて積分する
    float w0 = TWO_PI * g_Freq * sqrt(5.0 / h) / sqrt(max(s.flex, 0.2));
    int steps = clamp((int) ceil(dt / 0.008), 1, 8);
    float hdt = dt / (float) steps;
    for (int k = 0; k < steps; ++k)
    {
        float2 acc = (target - s.lean) * (w0 * w0) - s.leanVel * (2.0 * g_Damping * w0) + blast / h;
        s.leanVel += acc * hdt;
        s.lean += s.leanVel * hdt;
    }
    // 倒れすぎない
    float ll = length(s.lean);
    float cap = h * 0.45;
    if (ll > cap)
    {
        s.lean *= cap / ll;
        s.leanVel *= 0.5;
    }

    float rate = 1.0 - exp(-3.0 * dt);
    s.air = lerp(s.air, air, rate);
    s.windAmt = lerp(s.windAmt, speed / max(g_WindRef, 0.1), rate);
    g_States[i] = s;
}
