// ============================================================
// SwarmSpawnEnemyCS.hlsl
// 保留中の敵の生成リクエストをプールの空きスロットに入れる。
//
// SwarmSpawnProjCS と同じ骨組み: リクエスト 1 つに 1 スレッド、
// スレッドごとのオフセットから線形に走査し、状態バッファへの
// InterlockedCompareExchange で確保する。
//
// リクエストには CPU から位置 / hp / moveSpeed がもう入っている。
// velocity と yaw は 0 から始め、次の固定ステップで
// AI のパスが埋める。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> spawnRequests : register(t0);
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWBuffer<uint> enemyStates : register(u1);
// スロットごとの生成時の hp（固定小数点、SwarmEnemy.hp と同じ）。HP バー
// （SwarmEnemyHpBarVS）がこれで割る。SwarmEnemy を 48B のままにするための並列バッファ
RWStructuredBuffer<uint> enemyMaxHp : register(u2);
// スロットごとの種類 + 導火線。リクエストは種類を animIndex に入れて運ぶ
RWStructuredBuffer<SwarmEnemyExtra> enemyExtra : register(u3);
// スロットごとの毒の減速（SwarmAreaDamageCS を参照）: 新しい敵は減速なしで始まる
RWStructuredBuffer<float4> enemySlow : register(u4); // 減速 + 凍結（2026-10-08 から float4）
RWStructuredBuffer<float4> enemyPush : register(u5); // ノックバック（突風、2026-10-12）: 新しい敵には無い
RWStructuredBuffer<float4> enemyPoison : register(u6); // 溜まった毒素（毒沼、2026-10-17）: これも無し

cbuffer SwarmSpawnCB : register(b1)
{
    uint g_RequestCount;
    uint g_ScanStart;
    uint2 _spawnPad;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_RequestCount)
        return;

    SwarmEnemy req = spawnRequests[id.x];
    SwarmEnemyExtra extra;
    extra.kind = req.animIndex;
    extra.fuse = 0.0;
    req.animIndex = 0u;

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
            enemies[slot] = req;
            enemyMaxHp[slot] = req.hp;
            enemyExtra[slot] = extra;
            enemySlow[slot] = float4(0, 0, 0, 0);
            enemyPush[slot] = float4(0, 0, 0, 0);
            enemyPoison[slot] = float4(0, 0, 0, 0);
            return;
        }
    }

    // プールが満杯: 捨てる。弾のプールと同じ方針 --
    // 劣化はさせても、壊しはしない
}