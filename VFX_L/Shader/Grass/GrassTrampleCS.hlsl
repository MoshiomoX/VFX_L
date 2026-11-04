// ============================================================
// GrassTrampleCS.hlsl
// 踏み跡マップ：マップ全体の小区画ごとに 1 テクセル、
// xy = 草が押される方向、長さ = 強さ（0..1）。
// 毎フレーム古い値が薄れ（g_TrampleDecay）、スタンプが
// 押す：プレイヤーの円（中心から外向き）、突風の魔法
//（円か扇形）、カメラを揺らす生きている GPU 範囲
//（2026-10-13：爆発は円を外向きに倒し、光線のカプセルは
// 帯を横向きに曲げる）。強い方の押しが勝つ。
// 敵の足は後で GrassStampCS が押す（scatter）。
// 2026-10-23：風源（竜巻、渦、砂旋、
// 突風）が気流に沿って草を押すので、通り過ぎた竜巻は
// 足跡と同じように起き上がる渦巻きの跡を残す。
// ピンポン：前フレームのマップを読み、もう一方に書く（D3D11 では
// R16G16_FLOAT の typed UAV load が保証されないため）。
// ============================================================
#include "GrassCommon.hlsli"
#include "../Common/WindSources.hlsli"

// = SwarmArea（Shader/Common/SwarmCommon.hlsli）、48 バイト。読むのは center / radius / flags だけ
struct TrampleArea
{
    float3 center;
    float radius;
    float damage;
    float timeLeft;
    float tickInterval;
    float tickTimer;
    float halfHeight;
    uint flags;
    uint vfxType;
    uint tickNow;
};
#define TRAMPLE_MAX_AREAS 256u   // = SWARM_MAX_AREAS
#define TRAMPLE_AREA_CAPSULE 4u  // SWARM_AREA_CAPSULE（光線）
#define TRAMPLE_AREA_SHAKE 8u    // SWARM_AREA_SHAKE（爆発と光線）
#define TRAMPLE_MAX_SOURCES 32u

Texture2D<float2> g_TrampleIn : register(t0);
StructuredBuffer<TrampleArea> g_Areas : register(t1);
Buffer<uint> g_AreaStates : register(t2);          // 0 = 死亡
StructuredBuffer<float4> g_AreaEnds : register(t3); // スロットごとのカプセルの終点（AreaTickCS）
StructuredBuffer<WindSource> g_WindSources : register(t4);   // 未バインド = 風源なし（半径 0）
RWTexture2D<float2> g_TrampleOut : register(u0);

groupshared float4 gs_SrcA[TRAMPLE_MAX_SOURCES];   // 円：(cx, cz, 0, 0)。カプセル：(sx, sz, ex, ez)
groupshared float2 gs_SrcB[TRAMPLE_MAX_SOURCES];   //（半径、0 = 円 / 1 = カプセル）
groupshared uint gs_SrcCount;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex, uint3 gid : SV_GroupID)
{
    uint w, h;
    g_TrampleOut.GetDimensions(w, h);

    // ---- 生きている揺れる範囲を共有メモリに詰める（64 スレッド、各 4 範囲） ----
    // 2026-10-25：このグループの 8 x 8 テクセルに届くものだけ（テクセル中心での箱の判定。
    // 箱の外の発生源の押しはここのどのテクセルでも 0 なので結果は同じで、
    // 爆発が多くても遠くのもので 32 スロットが埋まらなくなる）
    float2 texelSize = g_MapSize / float2(w, h);
    float2 boxMin = g_MapOrigin + (float2(gid.xy * 8u) + 0.5) * texelSize;
    float2 boxMax = boxMin + 7.0 * texelSize;
    if (gi == 0u)
        gs_SrcCount = 0u;
    GroupMemoryBarrierWithGroupSync();
    if (g_TrampleExtra.x > 0.0 || g_TrampleExtra.y > 0.0)
    {
        for (uint a = gi; a < TRAMPLE_MAX_AREAS; a += 64u)
        {
            if (g_AreaStates[a] == 0u)
                continue;
            TrampleArea ar = g_Areas[a];
            if ((ar.flags & TRAMPLE_AREA_SHAKE) == 0u)
                continue;
            bool capsule = (ar.flags & TRAMPLE_AREA_CAPSULE) != 0u;
            float r = ar.radius * (capsule ? g_TrampleExtra.y : g_TrampleExtra.x);
            if (r <= 0.0)
                continue;
            float4 e = g_AreaEnds[a];
            float reach = r + 0.01;   // 少しの余裕（ここと下ではテクセル中心の計算の仕方が違う）
            float2 lo = (capsule ? min(ar.center.xz, e.xz) : ar.center.xz) - reach;
            float2 hi = (capsule ? max(ar.center.xz, e.xz) : ar.center.xz) + reach;
            if (any(hi < boxMin) || any(lo > boxMax))
                continue;
            uint slot;
            InterlockedAdd(gs_SrcCount, 1u, slot);
            if (slot < TRAMPLE_MAX_SOURCES)
            {
                gs_SrcA[slot] = capsule ? float4(ar.center.xz, e.xz) : float4(ar.center.xz, 0.0, 0.0);
                gs_SrcB[slot] = float2(r, capsule ? 1.0 : 0.0);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (id.x >= w || id.y >= h)
        return;

    float2 v = g_TrampleIn.Load(int3(id.xy, 0)) * g_TrampleDecay;
    float2 xz = g_MapOrigin + (float2(id.xy) + 0.5) / float2(w, h) * g_MapSize;
    // このフレームの突風（最大 GRASS_MAX_GUSTS 個、半径 0 = 未使用）
    [unroll]
    for (uint gk = 0u; gk < GRASS_MAX_GUSTS; ++gk)
    {
        float2 gRoot = g_GustA[gk].xy;
        float2 gDir = g_GustA[gk].zw;
        float gLength = g_GustB[gk].x;
        float gRadius = g_GustB[gk].y;
        // 突風：1 フレームだけのスタンプ、円全体を外向きに倒す（足跡と同じように起き上がる）
        if (gRadius > 0.0 && gLength <= 0.0)
        {
            float2 d = xz - gRoot;
            float dist = length(d);
            if (dist < gRadius && dist > 1e-3)
            {
                float s = 0.75 + 0.25 * sqrt(1.0 - dist / gRadius);
                float2 push = d / dist * s;
                if (dot(push, push) > dot(v, v))
                    v = push;
            }
        }
        // 前向きの突風：詠唱者から gDir 方向への扇形。根元は細く（0.35 r）、先端で
        // 半径 r いっぱい。草は風下へ倒れ、両脇では少し外へ開く
        if (gRadius > 0.0 && gLength > 0.0)
        {
            float2 d = xz - gRoot;
            float along = dot(d, gDir);
            if (along > -0.3 && along < gLength)
            {
                float2 sideV = d - gDir * along;
                float side = length(sideV);
                float t = saturate(along / gLength);
                float r = gRadius * lerp(0.35, 1.0, t);
                if (side < r)
                {
                    float s = 0.75 + 0.25 * sqrt(1.0 - side / r);
                    float2 dir = gDir;
                    if (side > 1e-3)
                        dir = normalize(gDir + sideV / side * (0.4 * side / r));
                    float2 push = dir * s;
                    if (dot(push, push) > dot(v, v))
                        v = push;
                }
            }
        }
    }
    // 爆発（範囲が生きている間、円を外向きに倒す）と光線（帯を横向きに押す）
    uint n = min(gs_SrcCount, TRAMPLE_MAX_SOURCES);
    for (uint k = 0u; k < n; ++k)
    {
        float4 A = gs_SrcA[k];
        float r = gs_SrcB[k].x;
        float2 push = 0.0;
        if (gs_SrcB[k].y > 0.5)
        {
            float2 seg = A.zw - A.xy;
            float t = saturate(dot(xz - A.xy, seg) / max(dot(seg, seg), 1e-4));
            float2 d = xz - (A.xy + seg * t);
            float side = length(d);
            if (side < r && side > 1e-3)
                push = d / side * (0.55 + 0.35 * sqrt(1.0 - side / r));
        }
        else
        {
            float2 d = xz - A.xy;
            float dist = length(d);
            if (dist < r && dist > 1e-3)
                push = d / dist * (0.8 + 0.2 * sqrt(1.0 - dist / r));
        }
        if (dot(push, push) > dot(v, v))
            v = push;
    }
    // 風源の跡：気流に沿って押す（竜巻のまわりの渦巻き）
    if (g_BladeShape.w > 0.0)
    {
        float lift;
        float3 air = WindSourceAir(g_WindSources, xz, lift);
        float s = length(air.xz);
        if (s > 0.5)
        {
            float2 push = air.xz / s * (saturate(s / max(g_DryColor.a, 0.1)) * g_BladeShape.w);
            if (dot(push, push) > dot(v, v))
                v = push;
        }
    }
    if (g_TrampleRadius > 0.0)
    {
        float2 d = xz - g_Player.xz;
        float dist = length(d);
        if (dist < g_TrampleRadius)
        {
            // 頂上が平らな減衰：円全体を押し、縁はやわらかく
            float s = sqrt(1.0 - dist / g_TrampleRadius);
            float2 push = (dist > 1e-3) ? d / dist * s : float2(0.0, 0.0);
            if (dot(push, push) > dot(v, v))
                v = push;
        }
    }
    g_TrampleOut[id.xy] = v;
}
