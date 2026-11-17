// ============================================================
// SwarmAreaSpawn.hlsli
// GPU 側で範囲を生成するバッファと関数。SWARM_AREA_POOL_U を定義したシェーダーだけが
// SwarmCommon.hlsli 経由で使う。
// ============================================================

#ifndef SWARM_AREA_POOL_U
#error "SWARM_AREA_POOL_U を定義してから include すること"
#endif

#ifndef SWARM_AREA_SPAWN_HLSLI
#define SWARM_AREA_SPAWN_HLSLI

// ---- GPU 側の生成 ----
// 範囲を生成するシェーダーは、このファイルを include する前に
// 3 つのレジスタを #define する。例：
//     #define SWARM_AREA_POOL_U  u6
//     #define SWARM_AREA_STATE_U u7
//     #define SWARM_AREA_DEF_T   t2
RWStructuredBuffer<SwarmArea> areas : register(SWARM_AREA_POOL_U);
RWBuffer<uint> areaStates : register(SWARM_AREA_STATE_U);
StructuredBuffer<SwarmAreaDef> areaDefs : register(SWARM_AREA_DEF_T);
// 任意：生成したものが進んでいた方向（範囲スロットごと）
// （xy = 単位 xz、w = 1「新しい」）。SwarmAreaTrackCS が範囲を最初に見た時に
// 一度だけ読み、w を消す。UAV に余裕のあるシェーダーは SWARM_AREA_DIR_U を
// #define する（ProjMoveCS：LOB の毒瓶がそこに落ちる）。HitCS は UAV が
// 残っていないので、その範囲は方向を持たない（液面は角度を自分で選ぶ）
#ifdef SWARM_AREA_DIR_U
RWStructuredBuffer<float4> areaDirs : register(SWARM_AREA_DIR_U);
#endif

// 他のプールと同じ CAS の走査。プールが満杯 -> 範囲なし（劣化するだけで、壊れはしない）。
// scale: 生成したものの大きさの倍率（弾の SwarmProjScale、1 = 作ったまま）
// boost: 弾の projBoost（SwarmBoostDamage / SwarmBoostDuration）、0 = なし。
//        持続の分は持続する範囲だけ伸ばす（一発の範囲はどのみち 1 回だけ判定）
// dir  : 生成したものの xz の速度（長さは任意）。SWARM_AREA_DIR_U がある時だけ保持
// extraOr: この範囲 1 つに足す SWARM_AREA_EXTRA_* のビット（それを残した弾の吸血の印）
void SwarmSpawnAreaFromDefX(uint defId, float3 pos, uint salt, float scale, uint boost, float2 dir, uint extraOr)
{
    if (defId == 0u)
        return;

    SwarmAreaDef d = areaDefs[defId];
    bool lasting = d.tickInterval < d.duration;

    SwarmArea a = (SwarmArea) 0;
    a.center = pos;
    a.radius = d.radius * scale;
    a.damage = d.damage * SwarmBoostDamage(boost);
    a.timeLeft = d.duration * (lasting ? SwarmBoostDuration(boost) : 1.0);
    a.tickInterval = d.tickInterval;
    a.tickTimer = 0.0;
    a.halfHeight = d.halfHeight * scale;
    a.flags = (d.flags & 0xFFFFu & ~SWARM_AREA_FOLLOW_PLAYER) // 命中から生まれた範囲：その場に留まる
        | (SwarmPackScale(scale, 0xFFFFu) << SWARM_SCALE_SHIFT);
    a.vfxType = d.vfxType;
    a.tickState = 0u; // 判定の状態は空から始める
    a.extra = d.extra | extraOr;
    a.pushQ = d.pushQ;
    a.pullQ = d.pullQ;

    uint start = (salt * 97u) % SWARM_MAX_AREAS;
    for (uint k = 0; k < SWARM_MAX_AREAS; ++k)
    {
        uint slot = (start + k) % SWARM_MAX_AREAS;
        uint was;
        InterlockedCompareExchange(areaStates[slot], SWARM_DEAD, SWARM_ALIVE, was);
        if (was == SWARM_DEAD)
        {
            areas[slot] = a;
#ifdef SWARM_AREA_DIR_U
            float dirLenSq = dot(dir, dir);
            areaDirs[slot] = float4((dirLenSq > 1e-8) ? dir * rsqrt(dirLenSq) : float2(0.0, 0.0), 0.0, 1.0);
#endif
            return;
        }
    }
}
void SwarmSpawnAreaFromDef(uint defId, float3 pos, uint salt, float scale, uint boost, float2 dir)
{
    SwarmSpawnAreaFromDefX(defId, pos, salt, scale, boost, dir, 0u);
}

#endif
