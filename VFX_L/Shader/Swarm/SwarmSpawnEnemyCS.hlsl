// ============================================================
// SwarmSpawnEnemyCS.hlsl
// 保留中の敵の生成リクエストをプールの空きスロットに入れる。
//
// SwarmSpawnProjCS と同じ骨組み: リクエスト 1 つに 1 スレッド、
// スレッドごとのオフセットから線形に走査し、状態バッファへの
// InterlockedCompareExchange で確保する。
//
// リクエスト（SwarmEnemySpawn）には CPU から位置 / hp / moveSpeed / yaw / 種類が入っている。
// velocity は 0 から始め、次の固定ステップで AI のパスが埋める。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemySpawn> spawnRequests : register(t0);
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWBuffer<uint> enemyStates : register(u1);
// スロットごとの生成時の hp（固定小数点、SwarmEnemy.hp と同じ）。HP バー
// （SwarmEnemyHpBarVS）がこれで割る。SwarmEnemy を 48B のままにするための並列バッファ
RWStructuredBuffer<uint> enemyMaxHp : register(u2);
// スロットごとの種類 + 導火線
RWStructuredBuffer<SwarmEnemyExtra> enemyExtra : register(u3);
// スロットごとの毒の減速（SwarmAreaDamageCS を参照）: 新しい敵は減速なしで始まる
RWStructuredBuffer<float4> enemyStatus : register(u4); // 減速 + 凍結
RWStructuredBuffer<float4> enemyPush : register(u5); // ノックバック（突風）: 新しい敵には無い
RWStructuredBuffer<float2> enemyPoison : register(u6); // 溜まった毒素（毒沼）: これも無し
RWStructuredBuffer<SwarmEnemyHitInfo> enemyHitInfo : register(u7); // 被弾の記録: これも無し

#include "SwarmSpawnCB.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_RequestCount)
        return;

    SwarmEnemySpawn req = spawnRequests[id.x];
    SwarmEnemy e = SwarmEnemyFromSpawn(req);
    SwarmEnemyExtra extra;
    extra.kind = req.kind;
    extra.fuse = 0.0;

    // 開始位置をばらして、スレッドごとに別の区間を確保させる
    uint start = (g_ScanStart + id.x * 97u) % g_MaxEnemies;

    for (uint i = 0; i < g_MaxEnemies; ++i)
    {
        uint slot = (start + i) % g_MaxEnemies;

        uint prev;
        InterlockedCompareExchange(enemyStates[slot],
                                   SWARM_DEAD, SWARM_ALIVE, prev);

        if (prev == SWARM_DEAD)
        {
            // スロットを取れた。状態は交換でもう ALIVE になっている
            enemies[slot] = e;
            enemyMaxHp[slot] = req.hp;
            enemyExtra[slot] = extra;
            enemyStatus[slot] = float4(0, 0, 0, 0);
            enemyPush[slot] = float4(0, 0, 0, 0);
            enemyPoison[slot] = float2(0, 0);
            enemyHitInfo[slot] = (SwarmEnemyHitInfo) 0;
            return;
        }
    }

    // プールが満杯: 捨てる。弾のプールと同じ方針 --
    // 劣化はさせても、壊しはしない
}