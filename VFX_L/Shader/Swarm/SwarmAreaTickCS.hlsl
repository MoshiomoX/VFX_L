// ============================================================
// SwarmAreaTickCS.hlsl
// 範囲の時計を 1 固定ステップ進める。範囲のスロット 1 つにつき 1 スレッド。
// HitCS の後（命中で生まれた範囲が、生まれたそのステップで
// tick するように）、AreaDamageCS の前に走る。
//
//   - 時間切れの範囲は「ここ」、次のステップの頭で死ぬ。なので
//     最後の tick は AreaDamageCS でちゃんと適用済み
//   - FOLLOW_PLAYER の範囲はプレイヤーの位置へ動かす
//   - CAPSULE の範囲（光線）は始点 / 半径 / 終点を SwarmBeamCB
//     （b3、CPU が毎フレーム書く）から取り、CPU がチャンネルを
//     無効にしたら死ぬ。終点は AreaDamageCS 用に areaEnds（u3）へ
//   - tickNow の bit 0 は「このステップでダメージを与える」、bit 1 は「最初の tick」。
//     上位ビットは範囲の追加情報（毒素 / 飛沫、生成時に設定）で、
//     触らない（SwarmCommon.hlsli の SWARM_AREA_TICK_*）
//
// SwarmArea.tickNow の tick ビット / tickTimer / timeLeft を書くのはここだけ。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

RWStructuredBuffer<SwarmArea> areas : register(u0);
RWBuffer<uint> areaStates : register(u1);
RWByteAddressBuffer counters : register(u2);
RWStructuredBuffer<float4> areaEnds : register(u3); // カプセルの終点（xyz）、CAPSULE の範囲でだけ意味がある

cbuffer SwarmBeamCB : register(b3)
{
    float4 g_BeamStart[SWARM_MAX_BEAMS]; // xyz = 始点、w = 半径
    float4 g_BeamEnd[SWARM_MAX_BEAMS]; // xyz = 終点、w = 光線が出ている間は 1
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= SWARM_MAX_AREAS)
        return;
    if (areaStates[i] == SWARM_DEAD)
        return;

    SwarmArea a = areas[i];

    if (a.timeLeft <= 0.0)
    {
        areaStates[i] = SWARM_DEAD;
        return;
    }

    if ((a.flags & SWARM_AREA_FOLLOW_PLAYER) != 0u)
    {
        // 高さは作ったまま、XZ だけプレイヤーに乗る
        a.center.x = g_PlayerPos.x;
        a.center.z = g_PlayerPos.z;
    }
    if ((a.flags & SWARM_AREA_CAPSULE) != 0u)
    {
        uint ch = (a.flags >> SWARM_AREA_BEAM_SHIFT) & 0xFu;
        float4 s = g_BeamStart[ch];
        float4 e = g_BeamEnd[ch];
        if (ch >= SWARM_MAX_BEAMS || e.w <= 0.0)
        {
            areaStates[i] = SWARM_DEAD; // CPU が光線を終えた（またはこのチャンネルを持っていない）
            return;
        }
        a.center = s.xyz;
        a.radius = s.w;
        areaEnds[i] = float4(e.xyz, 1.0);
    }

    // tickNow の上位ビットは範囲の追加情報（毒素、飛沫のみ）：残して tick のビットだけ変える
    a.tickNow &= ~(SWARM_AREA_TICK_NOW | SWARM_AREA_TICK_FIRST);
    a.tickTimer -= g_Step;
    if (a.tickTimer <= 0.0)
    {
        a.tickNow |= SWARM_AREA_TICK_NOW;
        if ((a.tickNow & SWARM_AREA_TICK_SEEN) == 0u)
            a.tickNow |= SWARM_AREA_TICK_FIRST | SWARM_AREA_TICK_SEEN; // 泥沼はこの tick でだけダメージを与える
        a.tickTimer += max(a.tickInterval, g_Step);
    }

    a.timeLeft -= g_Step;

    areas[i] = a;

    uint prev;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_AREAS, 1u, prev);
    if ((a.tickNow & SWARM_AREA_TICK_NOW) != 0u)
        counters.InterlockedAdd(SWARM_CNT_TICKING_AREAS, 1u, prev);
}
