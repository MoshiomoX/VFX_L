// ============================================================
// Items/JumpCountUp.h
// レベルアップの候補「跳躍回数 +1」（2026-09-29 用户の依頼）。
//
// 設計意図：
//   空中でもう一回跳べる。空中の n 回目は跳躍力 × 0.75^n（12 → 9 → 6.75 …）なので、
//   重ねても高さは頭打ちになり、崖を無限に登れるようにはならない。
//   段差・台地の縁で届かなかった時の足し、滑りから跳んだ後の向き直しに使う
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline StatItemDef MakeJumpCountUp()
{
    StatItemDef def;

    def.common.id = ItemID::JumpCountUp;
    def.common.name = "Extra Jump +1";
    def.common.displayName = L"跳躍回数 +1";
    def.common.description = L"空中でもう一度跳べる。回を重ねるごとに勢いは少しずつ弱まる。";
    def.common.category = ItemCategory::Stat;
    def.common.occupyCells = {};                         // 背包に置かないので形は無い
    def.common.influenceCells = {};
    def.common.color = { 0.60f, 0.90f, 0.95f, 1.0f };
    def.common.iconPath = Res::Icon::JumpCountUp;

    def.kind = StatKind::JumpCount;
    def.amount = 1.0f;
    def.percent = false;
    def.cardLabel = L"跳躍回数";

    return def;
}
