// ============================================================
// GrassStampCS.hlsl
// 踏み跡マップ上の敵の足（2026-10-13）。GPU の群れの敵スロット 1 つに
// 1 スレッド：生きていて接地している敵は、自分の下のテクセルに小さな円
//（中心から外向きに押す）をスタンプする。同じ出力テクスチャに対して
// GrassTrampleCS の直後に走る：scatter
// 書き込み（テクセルは読み返さない。R16G16_FLOAT の typed UAV load は
// 保証されないため）なので、敵が立っている所では足跡が gather パスの
// 書いた値をそのまま置き換える。何百もの
// 敵が草の中を歩くので、これでフィールドが
// 踏み荒らされたように見える。
// 2026-10-25：種類は SwarmCommon.hlsli から取る（以前のローカルの表は
// ステージの固定エリートと、草の上にいない種類が
// 抜けていた）。足跡を付けないもの：最終ウェーブの幽霊 2 種（浮いている）、
// 砂嵐の術師の砂旋（その風源がもう草を押している）、
// 潜っている砂潜り / 砂の守護者、消えている影の剣士、そして
// 足が下の地面より明らかに上にあるもの（飛んでいる、または
// 急降下中のガーゴイル、段から落ちている雑魚）。
// ============================================================
// SwarmCommon の cbuffer はここでは使わない。b0（GrassCB）からずらす
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"
#include "GrassCommon.hlsli"

StructuredBuffer<SwarmEnemy> g_Enemies : register(t0);
Buffer<uint> g_EnemyStates : register(t1);         // 0 = 死亡
StructuredBuffer<SwarmEnemyExtra> g_EnemyExtra : register(t2);
Texture2D<float> g_Height : register(t3);          // GridWorld の高さ、テクセル = 高さセル（GrassCullCS も同じものを読む）
RWTexture2D<float2> g_TrampleOut : register(u0);

// 中心の下の生の高さセルより足がこれ以上高い = 空中（足跡なし）。段々の 1 段は
// 2.5m、その縁に立つ敵は生のセルと同じ高さ、飛んでいるガーゴイルは g_GargoyleHover だけ上
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
        || (SwarmIsFixedElite(kind) && ex.fuse < -0.5))   // 影の剣士が消えている（= SwarmUntargetable）
        return;

    float3 p = g_Enemies[j].position;
    uint hw, hh;
    g_Height.GetDimensions(hw, hh);
    int2 hc = int2(floor((p.xz - g_MapOrigin) / g_MapSize * float2(hw, hh)));
    hc = clamp(hc, int2(0, 0), int2((int) hw - 1, (int) hh - 1));
    // position.y はカプセルの中心（足 + groundY）：10-25〜10-28 はこれをそのまま地面と比べていたので、
    // 全ての敵が 0.9m 上 =「空中」になり、どの敵も草を踏んでいなかった
    if (p.y - g_SwarmExtra.x - g_Height.Load(int3(hc, 0)) > STAMP_AIR_HEIGHT)
        return;

    // 足跡：種類ごとの体の半径（大きい敵は広い円を押す）
    float r = g_TrampleExtra.w;
    if (kind == SWARM_KIND_BOSS) r *= 3.0;
    else if (SwarmIsBigElite(kind)) r *= 2.2;   // エリートとステージの固定エリート
    else if (kind == SWARM_KIND_BRUTE) r *= 1.3;

    uint w, h;
    g_TrampleOut.GetDimensions(w, h);
    float2 texel = g_MapSize / float2(w, h);
    float2 c = p.xz;
    float2 cuv = (c - g_MapOrigin) / texel;        // テクセル空間
    int2 lo = int2(floor(cuv - r / texel));
    int2 hi = int2(floor(cuv + r / texel));
    lo = max(lo, int2(0, 0));
    hi = min(hi, int2((int) w - 1, (int) h - 1));
    if (hi.x - lo.x > 12 || hi.y - lo.y > 12)      // 安全策：ボスでも幅は最大 ~7 テクセル
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
