// ============================================================
// Items/Poison.h
// 毒：基本魔法（2026-10-01）。一番近い敵の足元へ毒の塊を山なりに投げ、落ちた所に毒の池を残す。
//
// 設計意図（ユーザー 10-01：錬金術の毒瓶のような効果。瓶のモデルである必要は無い）：
//   ・弾は Lob 型（profile "Poison"）：撃った時の最寄りの敵の位置へ銃口から山なりに飛び、
//     途中の敵には当たらない。威力は池だけ
//   ・池（範囲 PoisonPool）：4 秒、0.5 秒ごとに威力 3、中の敵は移動が 40% 遅くなる
//     （エリート・ボスは半分。ユーザー 10-01 決定）。池を重ねるとダメージは重なり、減速は一番強い物だけ
//   ・基本魔法。形は 4 マスの凸字（2026-10-04、強さで形を決めた時に一番強い組）、影響マスは形の上下左右
//     （今のところ誘発する上級魔法は無い）
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
    def.common.description = L"毒の瓶を一番近い敵の足元へ投げ、毒沼を残す。毒沼の中の敵は動きが鈍くなり、継続ダメージを受ける。";
    def.common.iconPath = Res::Icon::Poison;
    def.common.category = ItemCategory::Projectile;
    // 形は強さで決める（2026-10-04）。池 1 つで敵 1 体に毎秒 6・範囲・減速 40% と、総合では一番強い →
    // 4 マスで一番詰めにくい凸字（首 + 胴 = 薬瓶）
    def.common.occupyCells = { { -1, 0 }, { 0, -1 }, { 0, 0 }, { 0, 1 } };
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);   // 誘発の届く範囲（形の上下左右）
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
