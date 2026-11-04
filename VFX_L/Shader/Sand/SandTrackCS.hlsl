// ============================================================
// SandTrackCS.hlsl
// 砂に付く雑魚の足跡（World/SandField、2026-11-02）。敵スロット 1 つに
// 1 スレッドで、SandFieldCS のすぐ後に同じ凹みマップへ書く：砂の上に立つ
// 生きた敵は小さな凹みを押し付ける（既存の値との min。凹みマップは
// R32_FLOAT なので型付き UAV のロードが使える）。同じテクセルの 2 体は
// 競合するが問題ない（どちらも凹みを書く）。
// GrassStampCS と同じ種類は跡を残さない：幽霊、砂嵐の術師の
// 砂旋、潜っている敵、消えている影の剣士、足が明らかに
// 地面より上にあるもの。
// ============================================================
// SwarmCommon の cbuffer はここでは使わない。b0 から外す（SandTrackCB）
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

// SandField::TrackCB（32 バイト）と一致させること
cbuffer SandTrackCB : register(b0)
{
    float2 g_MapOrigin;
    float2 g_MapSize;
    float g_Radius;         // m（雑魚 1 体分。大きい種類はもっと広い）
    float g_Depth;          // m（負）
    float g_TrackGroundY;   // AICB.groundY：position.y はカプセルの中心 = 足 + これ
    float g_On;
};

StructuredBuffer<SwarmEnemy> g_Enemies : register(t0);
Buffer<uint> g_EnemyStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> g_EnemyExtra : register(t2);
Texture2D<float> g_Height : register(t3);
RWTexture2D<float> g_Track : register(u0);

static const float TRACK_AIR_HEIGHT = 0.8;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint j = id.x;
    uint count, stride;
    g_Enemies.GetDimensions(count, stride);
    if (j >= count || g_On < 0.5)
        return;
    if (g_EnemyStates[j] == SWARM_DEAD)
        return;
    SwarmEnemyExtra ex = g_EnemyExtra[j];
    uint kind = ex.kind;
    if (SwarmIsGhost(kind) || kind == SWARM_KIND_TORNADO || SwarmBuried(ex)
        || (SwarmIsFixedElite(kind) && ex.fuse < -0.5))
        return;

    float3 p = g_Enemies[j].position;
    uint hw, hh;
    g_Height.GetDimensions(hw, hh);
    int2 hc = int2(floor((p.xz - g_MapOrigin) / g_MapSize * float2(hw, hh)));
    hc = clamp(hc, int2(0, 0), int2((int) hw - 1, (int) hh - 1));
    if (p.y - g_TrackGroundY - g_Height.Load(int3(hc, 0)) > TRACK_AIR_HEIGHT)
        return;

    float r = g_Radius;
    if (kind == SWARM_KIND_BOSS) r *= 3.0;
    else if (SwarmIsBigElite(kind)) r *= 2.2;
    else if (kind == SWARM_KIND_BRUTE) r *= 1.3;

    uint w, h;
    g_Track.GetDimensions(w, h);
    float2 texel = g_MapSize / float2(w, h);
    float2 cuv = (p.xz - g_MapOrigin) / texel;
    int2 lo = max(int2(floor(cuv - r / texel)), int2(0, 0));
    int2 hi = min(int2(floor(cuv + r / texel)), int2((int) w - 1, (int) h - 1));
    if (hi.x - lo.x > 12 || hi.y - lo.y > 12)
        return;

    for (int y = lo.y; y <= hi.y; ++y)
    {
        for (int x = lo.x; x <= hi.x; ++x)
        {
            float2 xz = g_MapOrigin + (float2(x, y) + 0.5) * texel;
            float q = length(xz - p.xz) / r;
            if (q >= 1.0)
                continue;
            float dent = g_Depth * (1.0 - q * q);
            int2 t = int2(x, y);
            g_Track[t] = min(g_Track[t], dent);
        }
    }
}
