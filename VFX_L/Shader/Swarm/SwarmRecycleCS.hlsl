// ============================================================
// SwarmRecycleCS.hlsl
// プールは満杯なのにディレクターがまだ湧かせたい時: 一番遠い敵を
// 殺して生成し直す代わりに、十分遠くにいる生きた敵が保留中の要求を
// 1 つ取り、それで自分を上書きする（位置、HP、速度 -- 全部）。
// ソートも空きスロットも要らない。
//
// 「十分遠い」は距離のしきい値で、「遠い順の N 体」ではない。
// しきい値を越えた中で早い者勝ち。誰も十分遠くなければ要求は
// 単に捨てる。群れ全体がプレイヤーに張り付いている時は
// それで意図どおり。
// ============================================================
#define SWARM_AI_CB_REG b2
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> spawnRequests : register(t0);
Buffer<uint> enemyStates : register(t1);
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWByteAddressBuffer claim : register(u1);
// HP バー用の、スロットごとの生成時 HP（SwarmSpawnEnemyCS を参照）
RWStructuredBuffer<uint> enemyMaxHp : register(u2);
// スロットごとの種類 + 導火線（SwarmSpawnEnemyCS を参照）。点火済みの自爆兵が
// 転送されると導火線は忘れる
RWStructuredBuffer<SwarmEnemyExtra> enemyExtra : register(u3);
// スロットごとの毒の減速: 転送された敵は新しい敵なので減速なし
RWStructuredBuffer<float4> enemySlow : register(u4); // 減速 + 凍結（2026-10-08 から float4）
RWStructuredBuffer<float4> enemyPush : register(u5); // ノックバック（突風、2026-10-12）: 減速と一緒に消す
RWStructuredBuffer<float4> enemyPoison : register(u6); // 溜まった毒素（2026-10-17）: これも消す
// ダメージ数字用の前フレームの HP（SwarmDamageTextCS、2026-10-25）: 新しい生成時 HP + SWARM_PREVHP_RECYCLED にして、
// 旧 HP と新 HP の差をここでダメージとして出さず、旧い敵の保留中の数字も捨てさせる。
// （状態ワード用の UAV はもう残っていない: その属性 / 吸血ビットは新しい敵が初めて当たった時に書き直されるまで残るが、
// HP が減る時は必ずその書き直しが伴う。毒素は上で消してある）
RWBuffer<uint> prevHp : register(u7);

cbuffer SwarmRecycleCB : register(b1)
{
    uint g_RecycleCount;
    uint g_RecycleOffset;
    float g_RecycleMinDistSq;
    uint _recyclePad;
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    float3 d = enemies[i].position - g_PlayerPos;
    float dSq = d.x * d.x + d.z * d.z;
    if (dSq < g_RecycleMinDistSq)
        return;
    // エリート / ボスは普通の雑魚へ転送しない: そのまま追い続ける。
    // 幽霊も同じ（まっすぐ飛んでくる）。スプリッターと分裂体は転送できる（2026-10-03: 種類番号が
    // 幽霊より後なので、旧来の「kind >= ELITE」判定だと遠くに置き去りにされていた）
    uint kind = enemyExtra[i].kind;
    if (SwarmIsBigElite(kind) || kind == SWARM_KIND_BOSS || SwarmIsGhost(kind) || kind == SWARM_KIND_TORNADO)
        return;

    uint prev;
    claim.InterlockedAdd(0, 1u, prev);
    if (prev >= g_RecycleCount)
        return;

    SwarmEnemy req = spawnRequests[g_RecycleOffset + prev];
    SwarmEnemyExtra extra;
    extra.kind = req.animIndex;
    extra.fuse = 0.0;
    req.animIndex = 0u;
    enemies[i] = req;
    enemyMaxHp[i] = req.hp;
    enemyExtra[i] = extra;
    enemySlow[i] = float4(0, 0, 0, 0);
    enemyPush[i] = float4(0, 0, 0, 0);
    enemyPoison[i] = float4(0, 0, 0, 0);
    prevHp[i] = SWARM_PREVHP_RECYCLED | (req.hp & ~SWARM_PREVHP_RECYCLED);
}