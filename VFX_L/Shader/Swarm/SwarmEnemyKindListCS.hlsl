// ============================================================
// SwarmEnemyKindListCS.hlsl
// SwarmEnemyCompactCS に入りきらなかった種類の描画リスト
//（2026-10-08）：あちらのパスは既に 8 本の UAV を使っていて、これは feature level 11.0 で
// コンピュートシェーダーが持てる上限。規則は同じ：敵スロット 1 つに 1 スレッド、
// 生きているスロットをその種類のリストに追加し、リストの
// 長さが各サブメッシュの間接描画の InstanceCount になる。
// CompactCS はこれらの種類を mobList から外す。その直後に走る。
//   chargerList : SWARM_KIND_CHARGER（専用のテクスチャ、突進の帯）
//   shieldList  : SWARM_KIND_SHIELD  （専用のテクスチャ、塔盾）
// Append のカウンタは、初期カウント 0 で UAV をバインドしてリセットする。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t1);
AppendStructuredBuffer<uint> chargerList : register(u0);
AppendStructuredBuffer<uint> shieldList : register(u1);
AppendStructuredBuffer<uint> spikeBallList : register(u2); // SWARM_KIND_SPIKEBALL：専用のメッシュ、ブロック人形の体はなし（2026-10-17）
AppendStructuredBuffer<uint> burrowerList : register(u3); // SWARM_KIND_BURROWER（専用のテクスチャ + 砂山）（2026-10-17）
AppendStructuredBuffer<uint> sandMageList : register(u4); // SWARM_KIND_SANDMAGE（専用のテクスチャ）（2026-10-17）
// u5 は空き：砂旋のリストはメッシュと一緒に無くなった（2026-10-20：砂旋は何も描かない。砂ぼこりと風は
// SwarmTornadoEmitCS / SwarmWindSourceCS から出る。CompactCS は mobList から外す）
AppendStructuredBuffer<uint> dashBomberList : register(u6); // SWARM_KIND_DASHBOMBER（専用のテクスチャ）（2026-10-18）
AppendStructuredBuffer<uint> bossList : register(u7);       // SWARM_KIND_BOSS：ステージのボスのスキン + 飾り（2026-10-24）

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    uint kind = enemyExtra[i].kind;
    if (kind == SWARM_KIND_CHARGER)
        chargerList.Append(i);
    else if (kind == SWARM_KIND_SHIELD)
        shieldList.Append(i);
    else if (kind == SWARM_KIND_SPIKEBALL)
        spikeBallList.Append(i);
    else if (kind == SWARM_KIND_BURROWER)
        burrowerList.Append(i);
    else if (kind == SWARM_KIND_SANDMAGE)
        sandMageList.Append(i);
    else if (kind == SWARM_KIND_DASHBOMBER)
        dashBomberList.Append(i);
    else if (kind == SWARM_KIND_BOSS)
        bossList.Append(i);
}
