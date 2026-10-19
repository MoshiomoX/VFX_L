// ============================================================
// Items/Poison.h
// 猛毒の沼：上級魔法（2026-10-17 に基本魔法「毒」から格上げ。ユーザー：「今の毒を強化して、下位を 2 つ考える」）。
//   泥沼と胞子の両方に隣り合っている時だけ発動し、どちらかの弾が消えた所（泥沼の落点 / 胞子の命中点）へ
//   プレイヤーの手から毒の瓶を山なりに投げ（Lob 型 + 着弾点指定）、落ちた所に大きな毒沼を残す。
//   ・沼（範囲 PoisonPool）：半径 3、6 秒、0.5 秒ごとに威力 6、減速 50%。威力の半分はすぐ、半分は毒素として溜めて 1 秒で削る
//     （AreaProfile::toxin）。毒素を持つ沼は同じ敵に重ならない（一番強い 1 つだけ）
//   ・2026-10-01〜10-16 は基本魔法「毒」（4 秒、0.5 秒ごとに 3、減速 40%、1.2 秒 / MP 12）だった。
//     単体 20 / 群れ 195 で「単体も弱くない + 群れに圧倒的 + 一番安い」と突出していたので、上級の段へ
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
    def.common.displayName = L"猛毒の沼";
    def.common.description = L"連鎖した場所へ猛毒の瓶を投げ、大きな毒沼を残す。沼の中の敵は遅くなり、毒がじわじわ回る。";
    def.common.iconPath = Res::Icon::Poison;
    def.common.category = ItemCategory::Projectile;
    // 4 マスの凸字（首 + 胴 = 薬瓶）。上級魔法は他を誘発しないので影響マスは無し
    def.common.occupyCells = { { -1, 0 }, { 0, -1 }, { 0, 0 }, { 0, 1 } };
    def.common.influenceCells = {};
    def.common.color = { 0.35f, 0.80f, 0.25f, 1.0f };   // 毒の緑

    // ---- どう撃つか ----
    def.baseStats.id = ItemID::Poison;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 2.0f;    // 誘発のクールダウン（メテオ 1.8 / アイススパイク 2.0 と同じ段）
    def.baseStats.manaCost = 20.0f;

    // ---- 何を撃つか ----
    def.profile = "Poison";   // Assets/Data/ProjectileData/Poison.json（Lob 型、hitArea = PoisonPool）

    // ---- 前提の基本魔法（両方の影響マスが届いている時だけ有効）----
    def.common.triggeredBy = { ItemID::Swamp, ItemID::Spore };

    return def;
}
