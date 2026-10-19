// ============================================================
// Items/Spore.h
// 胞子：基本魔法（2026-10-17。茨を廃止した後、猛毒の沼のもう一つの前提として
//   ユーザーが「胞子」案を選んだ）。毒の胞子の塊をゆっくり飛ばし（Fireball と同じ直進、速度は半分以下）、
//   当たった所に 2 秒だけ胞子の霧を残す（範囲 SporeCloud）。霧のダメージは全部「毒素」
//   （AreaProfile::toxin 1.0 = その場では減らず、1 秒かけてじわじわ減る）。
//   泥沼と組んで猛毒の沼（ItemID::Poison）を発動させる
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeSpore()
{
    ProjectileItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::Spore;
    def.common.name = "Spore";
    def.common.displayName = L"胞子";
    def.common.description = L"毒の胞子の塊をゆっくり飛ばす。当たった所に胞子が漂い、中の敵をじわじわ蝕む。";
    def.common.iconPath = Res::Icon::Spore;
    def.common.category = ItemCategory::Projectile;
    // 形は強さで決める（2026-10-04 の決まり）：霧 4 × 4 回 = 16 / 0.9 秒、ただし当たってから 2 秒かけて → 泥沼並み → 縦 2 マス
    def.common.occupyCells = { { 0, 0 }, { 1, 0 } };
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);   // 誘発の届く範囲（形の上下左右。猛毒の沼）
    def.common.color = { 0.55f, 0.80f, 0.30f, 1.0f };   // 黄緑

    // ---- どう撃つか ----
    def.baseStats.id = ItemID::Spore;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.9f;
    def.baseStats.manaCost = 9.0f;

    // ---- 何を撃つか ----
    def.profile = "Spore";   // Assets/Data/ProjectileData/Spore.json（直進・遅い、hitArea = SporeCloud）

    return def;
}
