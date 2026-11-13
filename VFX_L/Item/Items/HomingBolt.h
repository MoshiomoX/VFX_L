// ============================================================
// Items/HomingBolt.h
// 追尾弾："HomingFull" プロファイル。全追尾（相手が死んだら次を探す）。
//
// 設計意図：
//   ・必中に近いのでマナは高め。威力低め・寿命長めは HomingFull.json 側
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeHomingBolt()
{
    ProjectileItemDef def;

    def.common.id = ItemID::HomingBolt;
    def.common.name = "Homing Bolt";
    def.common.displayName = L"ホーミングボルト";
    def.common.description = L"狙った敵を逃さない魔法弾。外れにくいが、MP を多めに使う。";
    def.common.iconPath = Res::Icon::HomingBolt;
    def.common.category = ItemCategory::Projectile;
    // 横 2 マス（3x3 枠の真ん中に置いても枠に収まる）
    def.common.occupyCells = ItemShape::RowLine(2);
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);   // 誘発の届く範囲（形の上下左右。弧と組んで魔導光線を目覚めさせる）
    def.common.color = { 0.40f, 0.58f, 1.00f, 1.0f };   // 輝石の青（弾の見た目に合わせる）

    // ---- どう撃つか（弾の威力・速さ・見た目は profile 側）----
    def.baseStats.id = ItemID::HomingBolt;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.45f;   // 単体の魔法は速め
    def.baseStats.manaCost = 10.5f;

    def.profile = "HomingFull";   // Assets/Data/ProjectileData/HomingFull.json

    return def;
}
