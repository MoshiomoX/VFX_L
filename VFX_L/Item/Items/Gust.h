// ============================================================
// Items/Gust.h
// 突風：基本の範囲魔法（2026-10-12、ユーザー：「世界の風を入れたのだから風の魔法を」→ 3 案から「突風」を選択。
// 威力は普通の魔法より少し低いが、ちゃんとダメージはある）。
//
// 設計意図：
//   ・プレイヤーの前（一番近い敵の方へ forward だけ寄せた円）の敵をまとめて吹き飛ばす + 少しのダメージ。
//     幸存者系の定番の「囲まれた時の押し返し」。エリートは半分、ボスは動かない（減速・凍結と同じ決まり）
//   ・押す向きはプレイヤーから離れる方（GPU は g_PlayerPos を見る。AreaDamageCS → enemyPush → AICS）
//   ・単体 DPS は約 10（12 / 1.2 秒。追尾弾 18）。代わりに範囲の全員に当たる
//   ・見た目：草が踏み跡と同じ仕組みで一斉に倒れる（GrassRenderer::AddGust）+ 土煙と草屑の輪（VFXData/Gust.json）
//   ・形は強さで決める（2026-10-04 の決まり）：横 2 マス、影響マスは形の上下左右
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline AreaItemDef MakeGust()
{
    AreaItemDef def;

    def.common.id = ItemID::Gust;
    def.common.name = "Gust";
    def.common.displayName = L"突風";
    def.common.description = L"一番近い敵へ向けて突風を放ち、前方の敵をまとめて吹き飛ばす。";
    def.common.iconPath = Res::Icon::Gust;
    def.common.category = ItemCategory::Area;
    def.common.occupyCells = ItemShape::RowLine(2);
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);
    def.common.color = { 0.72f, 0.95f, 0.85f, 1.0f };   // 薄い青緑（風）

    // ---- 基礎値（プロファイル "Gust" があればそちらで上書き）----
    def.baseStats.id = ItemID::Gust;
    def.baseStats.radius = 3.4f;
    def.baseStats.duration = 0.75f;
    def.baseStats.tickInterval = 1.0f;
    def.baseStats.damagePerTick = 12.0f;
    def.baseStats.castInterval = 1.2f;
    def.baseStats.manaCost = 14.0f;
    def.baseStats.spawnAtTarget = false;   // プレイヤーの位置（forward で標的の方へ寄る）

    def.vfxId = VFXId::Gust;
    def.profile = "Gust";   // Assets/Data/AreaData/Gust.json

    return def;
}
