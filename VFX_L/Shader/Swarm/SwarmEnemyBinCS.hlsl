// ============================================================
// SwarmEnemyBinCS.hlsl
// 敵スロット 1 つにつき 1 スレッド。生きている敵を、いるセルの空間
// ハッシュのバケットに入れる（SwarmCommon の SWARM_BUCKET_CAP 参照）。
// cellCount はこのディスパッチの直前に CPU が 0 クリアする
// （ClearUnorderedAccessViewUint）。各固定ステップの最初に走らせ、
// AI と押し出しパスが同じスナップショットを読むようにする。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<SwarmEnemy> enemies : register(t1);

RWStructuredBuffer<uint> cellCount : register(u0);
RWStructuredBuffer<uint> cellItems : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    int cell = SwarmCellOf(enemies[i].position);
    if (cell < 0)
        return;

    uint slot;
    InterlockedAdd(cellCount[cell], 1u, slot);
    if (slot < SWARM_BUCKET_CAP)
        cellItems[(uint) cell * SWARM_BUCKET_CAP + slot] = i;
}
