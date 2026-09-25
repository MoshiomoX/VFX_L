// ============================================================
// Items/Fireball.h
// 火球：最も基本的な飛行物型の魔法。
//
// 設計意図：
//   ・単発・低コスト・高頻度。全ての機能型の実験台になる基準値
//   ・弾そのもの（飛び方・威力・速さ・見た目）は profile "Fireball"
//     （Assets/Data/ProjectileData/Fireball.json、投射物編集器で編集）
//   ・他を強化しないので influenceCells は空
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
    def.common.description = L"扱いやすい基本の攻撃魔法。消費が軽く、ルーンの強化を試しやすい。";
    def.common.iconPath = Res::Icon::Fireball;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = {};                        // 強化効果なし
    def.common.color = { 1.00f, 0.55f, 0.20f, 1.0f };   // 橙

    // ---- どう撃つか ----
    def.baseStats.id = ItemID::Fireball;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.5f;
    def.baseStats.manaCost = 10.0f;

    // ---- 何を撃つか ----
    def.profile = "Fireball";

    return def;
}
