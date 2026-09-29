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
    def.common.description = L"敵を追い続ける魔法弾。狙った敵が倒れても次の敵を探す。消費は重め。";
    def.common.iconPath = Res::Icon::HomingBolt;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::Single();   // 1 マス（最初から持っている一番軽い魔法。3x3 枠の真ん中に置かれる）
    def.common.influenceCells = {};
    def.common.color = { 0.75f, 0.45f, 1.00f, 1.0f };   // 紫

    // ---- どう撃つか（弾の威力・速さ・見た目は profile 側）----
    def.baseStats.id = ItemID::HomingBolt;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.45f;   // 単体の魔法は速め：0.6 → 0.45（2026-09-30）
    def.baseStats.manaCost = 10.5f;       // 間隔と同じ x0.75 で、毎秒の消費は変えない

    def.profile = "HomingFull";   // Assets/Data/ProjectileData/HomingFull.json

    return def;
}
