// ============================================================
// Items/Fireball.h
// 火球：基本魔法。単体に当たる火の弾。
//
// 設計意図：
//   ・単発・低コスト・高頻度。着弾の爆発は無い（命中範囲は FireballHit：威力 0 の小さな炎の閃きだけ）
//   ・弾そのもの（飛び方・威力・速さ・見た目）は profile "Fireball"
//     （Assets/Data/ProjectileData/Fireball.json、投射物エディタで編集）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeFireball()
{
    ProjectileItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::Fireball;
    def.common.name = "Fireball";
    def.common.displayName = L"ファイアボール";
    def.common.description = L"火球で敵一体を焼く、扱いやすい基本の攻撃魔法。";
    def.common.iconPath = Res::Icon::Fireball;
    def.common.category = ItemCategory::Projectile;
    // 2x2 の火の玉
    def.common.occupyCells = ItemShape::Rect(2, 2);
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);   // 誘発の届く範囲（形の上下左右）
    def.common.color = { 1.00f, 0.55f, 0.20f, 1.0f };   // 橙

    // ---- どう撃つか ----
    def.baseStats.id = ItemID::Fireball;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.375f;  // 単体になったので単体の規則（0.5 x0.75）
    def.baseStats.manaCost = 7.5f;        // 同上（10 x0.75。毎秒の消費は元のまま）

    // ---- 何を撃つか ----
    def.profile = "Fireball";

    return def;
}
