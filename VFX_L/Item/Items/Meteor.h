// ============================================================
// Items/Meteor.h
// 隕石："ExplosiveArc" プロファイル。弧を描いて飛び、命中・寿命切れで
// GPU が Explosion（Assets/Data/AreaData/Explosion.json）を出す。
//
// 設計意図：
//   ・直撃は弱く、爆発が本体。撃つ間隔を長くして重い一発にする
//   ・爆発の威力・半径は Area プロファイル側（編集器で調整）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeMeteor()
{
    ProjectileItemDef def;

    def.common.id = ItemID::Meteor;
    def.common.name = "Meteor";
    def.common.displayName = L"メテオ";
    def.common.description = L"間隔の長い重い一撃。着弾点で爆発し、周りの敵をまとめて巻き込む。";
    def.common.iconPath = Res::Icon::Meteor;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = {};
    def.common.color = { 1.00f, 0.30f, 0.15f, 1.0f };   // 赤橙

    // ---- どう撃つか（弾の威力・速さ・見た目は profile 側）----
    def.baseStats.id = ItemID::Meteor;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 1.2f;
    def.baseStats.manaCost = 20.0f;

    def.profile = "ExplosiveArc";   // Assets/Data/ProjectileData/ExplosiveArc.json

    return def;
}
