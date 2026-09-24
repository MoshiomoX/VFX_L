// ============================================================
// Items/HomingBolt.h
// 追尾弾："HomingFull" プロファイル。全追尾（相手が死んだら次を探す）。
//
// 設計意図：
//   ・必中に近いのでマナは高め。威力低め・寿命長めは HomingFull.json 側
// ============================================================
#pragma once
#include "Item/ItemTypes.h"

inline ProjectileItemDef MakeHomingBolt()
{
    ProjectileItemDef def;

    def.common.id = ItemID::HomingBolt;
    def.common.name = "Homing Bolt";
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = {};
    def.common.color = { 0.75f, 0.45f, 1.00f, 1.0f };   // 紫

    // ---- どう撃つか（弾の威力・速さ・見た目は profile 側）----
    def.baseStats.id = ItemID::HomingBolt;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.6f;
    def.baseStats.manaCost = 14.0f;

    def.profile = "HomingFull";   // Assets/Data/ProjectileData/HomingFull.json

    return def;
}
