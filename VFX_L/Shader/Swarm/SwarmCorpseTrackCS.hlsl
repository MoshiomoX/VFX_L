// ============================================================
// SwarmCorpseTrackCS.hlsl
// 1 フレームに 1 回、敵のスロット 1 つに 1 スレッド（2026-10-02、死亡時の砕け散り）。
// 前フレームは生きていて（prevAlive）今は DEAD のスロットは、前フレーム以降に
// 死んだ：最後の位置 / yaw / 種類はまだスロットに残っている（死んだスロットは
// 再利用されるまで誰も消さない）ので、SwarmCorpseVS 用の死体リングへ写し、
// 足元に土煙の範囲を置く
// （g_DeathArea、AreaDef「MobDeath」：ダメージ 0、見た目だけ）。
//
// 倒す側（HitCS / AreaDamageCS / ContactCS）でやらずにここでやる理由：
// HitCS は D3D11.0 の UAV 8 本をすでに使い切っていて、1 か所ならどんな死に方も
// 同じように拾える。KillAll は prevAlive も消すので、
// フィールドを一掃しても全員が一斉に砕けることはない。
//
// 同じフレーム内に死んで再利用されたスロットは取りこぼす（まれ）。
//
// 死んだスプリッター（2026-10-03）は分裂リングにも位置を書く
// （splitEvents：+0 にこれまでの書き込み総数、続いて 16 バイト × SWARM_MAX_SPLIT_EVENTS
// の float3 位置 + uint 種類）。CPU は連鎖リングと同じようにリードバックし、
// MobSpawner が分裂体を湧かせる。砕け散りを切っていてもこれは動く。
// ============================================================
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5   // ガーゴイルの飛ぶ高さ（死体は地面から飛び散らせる、2026-10-25）
#define SWARM_AREA_POOL_U u3
#define SWARM_AREA_STATE_U u4
#define SWARM_AREA_DEF_T t3
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmCorpseTrackCB : register(b4)
{
    float g_CorpseTime;   // SwarmSystem の時計（m_AnimClock）
    uint g_CorpseOn;      // 0 = prevAlive の更新だけする
    uint g_DeathArea;     // 土煙の AreaDef、0 = なし
    uint _corpseTrackPad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t2);

RWBuffer<uint> prevAlive : register(u0);
RWStructuredBuffer<SwarmCorpse> corpses : register(u1);
RWByteAddressBuffer corpseHead : register(u2);   // [0] = これまでに書いた死体の数（リングを回り込む）
RWByteAddressBuffer splitEvents : register(u5);  // 分裂リング、上の説明を参照

static const uint SWARM_MAX_SPLIT_EVENTS = 128u;   // Swarm::kMaxSplitEvents

uint CorpseHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;

    uint alive = (enemyStates[i] != SWARM_DEAD) ? 1u : 0u;
    uint was = prevAlive[i];
    prevAlive[i] = alive;
    if (was == 0u || alive != 0u)
        return;

    // ---- 前フレーム以降に死んだ ----
    SwarmEnemy e = enemies[i];
    SwarmEnemyExtra ex = enemyExtra[i];
    uint kind = ex.kind;
    // 空中で倒された（ガーゴイル 2026-10-25、飛ぶボス）：破片と土煙は真下の地面から出す
    e.position.y -= SwarmBodyHover(ex);
    // 呪いの鎧の 1 回目の死（2026-10-25）：砕けずに崩れて山になり、数秒後に CPU
    // （MobSpawner）がここに蘇った鎧を立ち上がらせる。山 / 土煙は CPU のエフェクト
    if (kind == SWARM_KIND_ARMOR)
    {
        uint s;
        splitEvents.InterlockedAdd(0, 1u, s);
        uint at = 16u + (s % SWARM_MAX_SPLIT_EVENTS) * 16u;
        splitEvents.Store3(at, asuint(e.position));
        splitEvents.Store(at + 12u, kind);
        return;
    }
    if (kind == SWARM_KIND_SPLITTER)
    {
        uint s;
        splitEvents.InterlockedAdd(0, 1u, s);
        uint at = 16u + (s % SWARM_MAX_SPLIT_EVENTS) * 16u;
        splitEvents.Store3(at, asuint(e.position));
        splitEvents.Store(at + 12u, kind);
    }
    // 砂嵐の術師の砂旋（2026-10-17）はただ吹き消える：土煙だけで砕けない（ブロックの体が無い）
    if (kind == SWARM_KIND_TORNADO)
    {
        if (g_DeathArea != 0u)
            SwarmSpawnAreaFromDef(g_DeathArea, e.position, i, 1.0, 0u, float2(0.0, 0.0));
        return;
    }
    if (g_CorpseOn == 0u)
        return;

    uint n;
    corpseHead.InterlockedAdd(0, 1u, n);

    SwarmCorpse c;
    c.position = e.position;
    c.yaw = e.yaw;
    float2 away = e.position.xz - g_PlayerPos.xz;
    float awayLenSq = dot(away, away);
    // プレイヤーから離れる向き（魔法はそちらから来る）。プレイヤーの真上なら後ろ向き
    c.dir = (awayLenSq > 1e-4) ? away * rsqrt(awayLenSq) : -float2(sin(e.yaw), cos(e.yaw));
    c.birth = g_CorpseTime;
    c.kind = kind;
    c.seed = CorpseHash(i * 2654435761u ^ (n * 0x9E3779B9u));
    c.cause = 0u;
    c._pad = float2(0.0, 0.0);
    corpses[n % SWARM_MAX_CORPSES] = c;

    if (g_DeathArea != 0u)
        SwarmSpawnAreaFromDef(g_DeathArea, e.position, i, 1.0, 0u, float2(0.0, 0.0));
}
