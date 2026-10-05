// ============================================================
// Items/ArcBolt.h
// 弧の矢：投射物エディタの "ArcOnce" プロファイルで飛ぶ飛行物型。
//
// 設計意図：
//   ・威力・速さ・見た目は ArcOnce.json 側（火球と同じ値にして飛び方だけ比べる）
//   ・単追尾（撃った瞬間の最近敵を捕捉、死んだら直進）
//   ・左右交互に弧を描く（mirror は DB が 1 発ごとに決める）
//   ・見た目は火球の VFX を流用。飛び方の差だけを見る
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeArcBolt()
{
    ProjectileItemDef def;

    def.common.id = ItemID::ArcBolt;
    def.common.name = "Arc Bolt";
    def.common.displayName = L"アークボルト";
    def.common.description = L"弧を描いて敵へ向かう魔法弾。撃つたびに左右が入れ替わる。";
    def.common.iconPath = Res::Icon::ArcBolt;
    def.common.category = ItemCategory::Projectile;
    // 形は強さで決める（2026-10-04）。単体 DPS 約 27（火球と並んで一番高い）→ 4 マスの稲妻の Z 字
    def.common.occupyCells = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 2 } };
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);   // 誘発の届く範囲（形の上下左右。追尾弾と組んで魔導光線を目覚めさせる）
    def.common.color = { 0.40f, 0.80f, 1.00f, 1.0f };   // 水色

    // ---- どう撃つか（弾の威力・速さ・見た目は profile 側）----
    def.baseStats.id = ItemID::ArcBolt;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.375f;  // 単体の魔法は速め：0.5 → 0.375（2026-09-30）
    def.baseStats.manaCost = 7.5f;        // 間隔と同じ x0.75 で、毎秒の消費は変えない

    // ---- 飛び方 ----
    def.profile = "ArcOnce";   // Assets/Data/ProjectileData/ArcOnce.json

    return def;
}
