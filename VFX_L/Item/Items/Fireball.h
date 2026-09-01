// ============================================================
// Items/Fireball.h
// 火球：最も基本的な飛行物型の魔法。
//
// 設計意図：
//   ・単発・低コスト・高頻度。全ての機能型の実験台になる基準値
//   ・見た目（0.9）は当たり判定（0.25）の約3.6倍。
//     派手に見せつつ、命中判定は安っぽくならないようにする
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
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = {};                        // 強化効果なし
    def.common.color = { 1.00f, 0.55f, 0.20f, 1.0f };   // 橙

    // ---- 基礎値 ----
    def.baseStats.id = ItemID::Fireball;
    def.baseStats.damage = 10.0f;
    def.baseStats.speed = 20.0f;
    def.baseStats.radius = 0.25f;   // 当たり判定
    def.baseStats.lifetime = 3.0f;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.5f;
    def.baseStats.manaCost = 10.0f;

    // ---- 見た目 ----
    def.visualSize = 0.9f;    // 当たり判定より意図的に大きく
    def.visualStretch = 0.0f;    // 円形
    def.vfxPath = Res::VFX::Fireball;

    return def;
}