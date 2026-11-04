// ============================================================
// SwarmLodSplitCS.hlsl
// 普通の雑魚の LOD（2026-11-02）。描画リストを作った後に
// 走る：mobList（SwarmEnemyCompactCS が雑魚リストに入れたスロット）を
// 読み、カメラまでの距離で分ける。
//   nearList : 従来どおり描く（体のパーツごとに 1 回の描画、パーツのアニメーション）
//   farList  : 結合したメッシュで描く（全パーツを 1 回の描画で、パーツの
//              アニメーションの代わりにプロシージャルな揺れ）
// mobList の長さは CopyStructureCount で listCount（最初の uint）に
// 入れたもの。リストの要素 1 つに 1 スレッド。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

cbuffer LodCB : register(b3)
{
    float3 g_LodCamPos;
    float g_LodDist2;      // 距離の 2 乗。これより先 = 遠い
};

StructuredBuffer<uint> mobList : register(t0);
ByteAddressBuffer listCount : register(t1);
StructuredBuffer<SwarmEnemy> enemies : register(t2);
AppendStructuredBuffer<uint> nearList : register(u0);
AppendStructuredBuffer<uint> farList : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint n = min(listCount.Load(0), g_MaxEnemies);
    if (id.x >= n)
        return;
    uint slot = mobList[id.x];
    float3 d = enemies[slot].position - g_LodCamPos;
    if (dot(d, d) > g_LodDist2)
        farList.Append(slot);
    else
        nearList.Append(slot);
}
