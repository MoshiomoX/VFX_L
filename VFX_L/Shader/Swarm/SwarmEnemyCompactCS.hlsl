// ============================================================
// SwarmEnemyCompactCS.hlsl
// 敵スロット 1 つに 1 スレッド：生きているスロットの番号を aliveList に
// 追加する。メッシュの描画は DrawIndexedInstancedIndirect で
// instanceCount = 一覧の長さ（CopyStructureCount）にするので、頂点
// シェーダーは 4096 スロットのプール全体ではなく実在する敵の分だけ
// 走る。約 9k 頂点のメッシュだと、1 フレームで 3500 万頂点か
// 約 100 万頂点かの差になる。
//
// 各スロットは自分の種類の一覧（mobList / bomberList）にも入る：
// 種類どうしでメッシュは同じだがテクスチャが違うので、メッシュは
// 種類の一覧ごとに 1 回描く。aliveList（全種類）は HP バーに使う。
//
// エリートは雑魚のメッシュ + テクスチャを共有するので mobList に入れる（VS が
// 種類に応じて拡大・着色する）。ボスは 2026-10-24 から専用の一覧を持つ
// （SwarmEnemyKindListCS：面ごとの皮 + 飾り）。
//
// ボスは bossInfo にも自分を書き込む（Swarm::BossInfo、32 バイト、
// このディスパッチの前に CPU がクリアし、ステージング経由でリードバックする）：
//   +0 生存数、+4 hp（固定小数）、+8 最大 hp（固定小数）、+16 位置
//
// Append のカウンタは、初期カウント 0 で UAV をバインドしてリセットする。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t1);
StructuredBuffer<SwarmEnemy> enemies : register(t2);
StructuredBuffer<uint> enemyMaxHp : register(t3);
AppendStructuredBuffer<uint> aliveList : register(u0);
AppendStructuredBuffer<uint> mobList : register(u1);
AppendStructuredBuffer<uint> bomberList : register(u2);
RWByteAddressBuffer bossInfo : register(u3);
AppendStructuredBuffer<uint> ghostList : register(u4); // 最後にアルファブレンドで描く
AppendStructuredBuffer<uint> splitterList : register(u5); // スプリッターと分裂体（専用テクスチャ、2026-10-03）
AppendStructuredBuffer<uint> bruteList : register(u6); // 重装兵（専用テクスチャ、2026-10-07）
// 突撃兵と盾兵（2026-10-08）は SwarmEnemyKindListCS へ：このパスには UAV の空きが無い（FL 11.0 で 8 本）

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    aliveList.Append(i);
    uint kind = enemyExtra[i].kind;
    if (kind == SWARM_KIND_BOMBER)
        bomberList.Append(i);
    else if (SwarmIsGhost(kind))
        ghostList.Append(i);   // 黒幽霊も同じ一覧（同じメッシュを半透明で描き、VS が種類に応じて着色する）
    else if (kind == SWARM_KIND_SPLITTER || kind == SWARM_KIND_SPLITLING)
        splitterList.Append(i);
    else if (kind == SWARM_KIND_BRUTE)
        bruteList.Append(i);
    else if (kind != SWARM_KIND_CHARGER && kind != SWARM_KIND_SHIELD && kind != SWARM_KIND_SPIKEBALL && kind != SWARM_KIND_BURROWER
          && kind != SWARM_KIND_SANDMAGE && kind != SWARM_KIND_TORNADO && kind != SWARM_KIND_DASHBOMBER
          && kind != SWARM_KIND_BOSS && !SwarmInKindList2(kind)) // トゲ玉はブロック人形の体を持たず（SwarmSpikeBallVS）、砂旋は何も描かない（2026-10-20）。それ以外は SwarmEnemyKindListCS に専用の一覧がある（ボスも、2026-10-24：面ごとの専用の皮 + 飾り）
        mobList.Append(i);

    // 最終ウェーブの幽霊は、bossInfo の空いている 2 ワード（オフセット 12 / 28）に種類ごとに数える：
    // CPU がそれぞれに専用の上限を持たせる（2026-10-18。以前は雑魚と上限を共有していて、ほとんど湧かなかった）
    if (kind == SWARM_KIND_GHOST)
    {
        uint prev;
        bossInfo.InterlockedAdd(12, 1u, prev);
    }
    else if (kind == SWARM_KIND_DARKGHOST)
    {
        uint prev;
        bossInfo.InterlockedAdd(28, 1u, prev);
    }
    if (kind == SWARM_KIND_BOSS)
    {
        uint prev;
        bossInfo.InterlockedAdd(0, 1u, prev);
        SwarmEnemy e = enemies[i];
        bossInfo.Store(4, e.hp);
        bossInfo.Store(8, enemyMaxHp[i]);
        bossInfo.Store3(16, asuint(e.position));
    }
}
