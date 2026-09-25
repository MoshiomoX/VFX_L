// ============================================================
// Items/ArcBolt.h
// 弧の矢：投射物編集器の "ArcOnce" プロファイルで飛ぶ飛行物型。
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
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = {};
    def.common.color = { 0.40f, 0.80f, 1.00f, 1.0f };   // 水色

    // ---- どう撃つか（弾の威力・速さ・見た目は profile 側）----
    def.baseStats.id = ItemID::ArcBolt;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.5f;
    def.baseStats.manaCost = 10.0f;

    // ---- 飛び方 ----
    def.profile = "ArcOnce";   // Assets/Data/ProjectileData/ArcOnce.json

    return def;
}
