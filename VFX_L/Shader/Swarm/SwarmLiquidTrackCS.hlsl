// ============================================================
// SwarmLiquidTrackCS.hlsl
// 毎フレーム 1 回、範囲スロット 1 つに 1 スレッド（2026-10-02、Liquid エントリ）。
// 前フレーム以降に生まれた範囲に気付き、スロットごとに次を保持する：
//   liquidTrack[slot] = (投げた向き x, 投げた向き z,
//                        最初に見たときの timeLeft, 前フレームの timeLeft)
// これで SwarmLiquidVS は水たまりの年齢（= 最初に見た値 - 今）と、投げられた
// 向きがわかる。時計は要らない：前フレームに空だった（w <= 0）か、timeLeft が
// 増えたなら、そのスロットには新しい範囲が入っている。
//
// 向きは範囲を生成した側から来る：SWARM_AREA_DIR_U 付きの SwarmSpawnAreaFromDef
//（ProjMoveCS、つまり LOB の瓶の着地）が
// areaDirs[slot] = (dir, 0, 1) を書く。取るのは w = 1（「新鮮」）の間だけで、
// w はここで消すので、同じスロットに向きなしで生成された後の範囲
//（HitCS には UAV の空きがない）が古い向きを引き継ぐことはない：
// (0, 0) になり、水たまりはシードから角度を選ぶ。液体かどうかに関係なく
// 全ての範囲を追うので、「新鮮」フラグが残りっぱなしになることはない。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
RWStructuredBuffer<float4> areaDirs : register(u0);
RWStructuredBuffer<float4> liquidTrack : register(u1);
// これまでに生まれた範囲の数（2026-10-03）。増える一方で、CPU は差分を読む：
//   [0]            = SWARM_AREA_SHAKE の範囲（SwarmSystem::ConsumeShakeAreas -> BattleCamera）
//   [1 + vfxType]  = GPU エフェクトのレシピごとの範囲（命中の火花、爆発、死亡時の煙、池）
//                    -> SwarmSystem::ConsumeAreaBirths -> 音（CPU は GPU の命中を見ないため）
// このパスが新しく生まれた範囲をちょうど 1 回ずつ見つけるので、ここで数える
static const uint SWARM_AREA_BIRTH_KINDS = 127u;   // = Swarm::kAreaBirthKinds
RWByteAddressBuffer areaBirths : register(u2);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= SWARM_MAX_AREAS)
        return;

    float4 t = liquidTrack[i];
    if (areaStates[i] == SWARM_DEAD)
    {
        t.w = 0.0;   // 空
        liquidTrack[i] = t;
        return;
    }

    float left = areas[i].timeLeft;
    if (t.w <= 0.0 || left > t.w + 1e-3)
    {
        float4 ad = areaDirs[i];
        float2 dir = (ad.w > 0.5) ? ad.xy : float2(0.0, 0.0);
        areaDirs[i] = float4(ad.xyz, 0.0);   // 消費済み
        t = float4(dir, left, left);
        uint prev;
        if ((areas[i].flags & SWARM_AREA_SHAKE) != 0u)
            areaBirths.InterlockedAdd(0, 1u, prev);
        uint kind = min(areas[i].vfxType, SWARM_AREA_BIRTH_KINDS - 1u);
        areaBirths.InterlockedAdd((1u + kind) * 4u, 1u, prev);
    }
    else
    {
        t.w = left;
    }
    liquidTrack[i] = t;
}
