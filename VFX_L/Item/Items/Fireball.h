// ============================================================
// Items/Fireball.h
// 火球：基礎魔法（2026-09-30〜）。単体に当たる火の弾。
//
// 設計意図：
//   ・単発・低コスト・高頻度。以前は着弾で爆発していたが、範囲が強すぎたので廃止
//     （命中範囲は FireballHit：威力 0 の小さな炎の閃きだけ）
//   ・影響格（上下左右）が届いている高級魔法を誘発する：
//     この弾が消えた場所（命中・寿命・壁）にメテオが落ちる（メテオには石弾も要る）
//   ・弾そのもの（飛び方・威力・速さ・見た目）は profile "Fireball"
//     （Assets/Data/ProjectileData/Fireball.json、投射物編集器で編集）
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
    def.common.description = L"敵一体を焼く基本の火の弾。上下左右に置いた上級魔法を、弾が消えた場所で呼び起こす。";
    def.common.iconPath = Res::Icon::Fireball;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::Single();       // 基礎魔法は 1 マス
    def.common.influenceCells = ItemShape::Cross();     // 誘発の届く範囲（上下左右）
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
