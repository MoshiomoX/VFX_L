// ============================================================
// Items/Poison.h
// 毒：基礎魔法（2026-10-01）。一番近い敵の足元へ毒の塊を山なりに投げ、落ちた所に毒の池を残す。
//
// 設計意図（用户 10-01：錬金術の毒瓶のような効果。瓶の模型である必要は無い）：
//   ・弾は Lob 型（profile "Poison"）：撃った時の最寄りの敵の位置へ銃口から山なりに飛び、
//     途中の敵には当たらない。威力は池だけ
//   ・池（範囲 PoisonPool）：4 秒、0.5 秒ごとに威力 3、中の敵は移動が 40% 遅くなる
//     （精鋭・ボスは半分。用户 10-01 決定）。池を重ねるとダメージは重なり、減速は一番強い物だけ
//   ・基礎魔法なので 1 マス・影響格は上下左右（今のところ誘発する上級魔法は無い）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakePoison()
{
    ProjectileItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::Poison;
    def.common.name = "Poison";
    def.common.displayName = L"毒";
    def.common.description = L"毒の塊を一番近い敵の足元へ投げつけ、毒の池を残す。池の中の敵は動きが鈍り、少しずつ体力を失う。";
    def.common.iconPath = Res::Icon::Poison;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::Single();       // 基礎魔法は 1 マス
    def.common.influenceCells = ItemShape::Cross();     // 誘発の届く範囲（上下左右）
    def.common.color = { 0.35f, 0.80f, 0.25f, 1.0f };   // 毒の緑

    // ---- どう撃つか ----
    def.baseStats.id = ItemID::Poison;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 1.2f;
    def.baseStats.manaCost = 12.0f;

    // ---- 何を撃つか ----
    def.profile = "Poison";   // Assets/Data/ProjectileData/Poison.json

    return def;
}
