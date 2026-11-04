// ============================================================
// SwarmEnemyKindList2CS.hlsl
// 遺跡の敵の描画リスト（2026-10-25）：SwarmEnemyCompactCS と
// SwarmEnemyKindListCS はどちらも既に 8 本の UAV を使っている。規則は同じ：
// 敵スロット 1 つに 1 スレッド、生きているスロットをその種類のリストに追加し、
// リストの長さが各サブメッシュの間接描画の InstanceCount に
// なる。CompactCS はこれらの種類を mobList から外す。
// SwarmEnemyKindListCS の直後に走る。
//   armorList    : SWARM_KIND_ARMOR と ARMOR2（蘇った方は光る）
//   gargoyleList : SWARM_KIND_GARGOYLE（翼）
//   necroList    : SWARM_KIND_NECRO（フード、杖）
//   skeletonList : SWARM_KIND_SKELETON
//   wardenList   : SWARM_KIND_WARDEN（鎖、鉤）
//   eliteAList   : SWARM_KIND_ELITE_A、ステージの近接の固定エリート（2026-10-25）
//   eliteBList   : SWARM_KIND_ELITE_B、遠隔の方
// 固定エリートは eliteInfo にも自分を報告する（Swarm::EliteInfoBlock：+0 に数、続いて
// 1 体 32 バイト = スロット、種類、hp（固定小数点）、最大 hp（固定小数点）、位置、fuse。このディスパッチの前に
// CPU が消し、EliteAttacks がステージング経由でリードバックする）
// Append のカウンタは、初期カウント 0 で UAV をバインドしてリセットする。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t1);
StructuredBuffer<SwarmEnemy> enemies : register(t2);
StructuredBuffer<uint> enemyMaxHp : register(t3);
AppendStructuredBuffer<uint> armorList : register(u0);
AppendStructuredBuffer<uint> gargoyleList : register(u1);
AppendStructuredBuffer<uint> necroList : register(u2);
AppendStructuredBuffer<uint> skeletonList : register(u3);
AppendStructuredBuffer<uint> wardenList : register(u4);
AppendStructuredBuffer<uint> eliteAList : register(u5);
AppendStructuredBuffer<uint> eliteBList : register(u6);
RWByteAddressBuffer eliteInfo : register(u7);

static const uint ELITE_INFO_MAX = 4u;   // = Swarm::kMaxEliteInfo

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    uint kind = enemyExtra[i].kind;
    if (kind == SWARM_KIND_ARMOR || kind == SWARM_KIND_ARMOR2)
        armorList.Append(i);
    else if (kind == SWARM_KIND_GARGOYLE)
        gargoyleList.Append(i);
    else if (kind == SWARM_KIND_NECRO)
        necroList.Append(i);
    else if (kind == SWARM_KIND_SKELETON)
        skeletonList.Append(i);
    else if (kind == SWARM_KIND_WARDEN)
        wardenList.Append(i);
    else if (SwarmIsFixedElite(kind))
    {
        if (kind == SWARM_KIND_ELITE_A)
            eliteAList.Append(i);
        else
            eliteBList.Append(i);
        uint n;
        eliteInfo.InterlockedAdd(0, 1u, n);
        if (n < ELITE_INFO_MAX)
        {
            uint at = 16u + n * 32u;
            SwarmEnemy e = enemies[i];
            eliteInfo.Store4(at, uint4(i, kind, e.hp, enemyMaxHp[i]));
            eliteInfo.Store4(at + 16u, uint4(asuint(e.position), asuint(enemyExtra[i].fuse)));
        }
    }
}
