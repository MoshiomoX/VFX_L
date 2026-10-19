// ============================================================
// Items/Swamp.h
// 泥沼：基本魔法（2026-10-17、ユーザー：「毒を上級魔法に格上げして、下位を 2 つ作る」の片方）。
//   一番近い敵の足元へ泥の瓶を山なりに投げ、落ちた時だけ飛沫の範囲ダメージ（低め）、
//   残る沼は減速だけ（AreaProfile::splashOnly、範囲 SwampPool）。
//   胞子と組んで猛毒の沼（ItemID::Poison）を発動させる
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeSwamp()
{
    ProjectileItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::Swamp;
    def.common.name = "Swamp";
    def.common.displayName = L"泥沼";
    def.common.description = L"泥の瓶を投げて沼を作る。落ちた時に周りを傷つけ、沼の中の敵は足を取られて遅くなる。";
    def.common.iconPath = Res::Icon::Swamp;
    def.common.category = ItemCategory::Projectile;
    // 形は強さで決める（2026-10-04 の決まり）：飛沫 10 / 1.2 秒 ≒ 8 + 減速 → ホーミングボルト並み → 横 2 マス
    def.common.occupyCells = { { 0, 0 }, { 0, 1 } };
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);   // 誘発の届く範囲（形の上下左右。猛毒の沼）
    def.common.color = { 0.55f, 0.40f, 0.20f, 1.0f };   // 泥の茶色

    // ---- どう撃つか ----
    def.baseStats.id = ItemID::Swamp;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 1.2f;
    def.baseStats.manaCost = 10.0f;

    // ---- 何を撃つか ----
    def.profile = "Swamp";   // Assets/Data/ProjectileData/Swamp.json（Lob 型、hitArea = SwampPool）

    return def;
}
