// ============================================================
// Items/MaxManaUp.h
// レベルアップの候補「魔力の上限 +20」。
//
// 設計意図：
//   魔法が増えるほど魔力が足りなくなる。
//   上限を広げると連続で撃てる数が増え、重い魔法も置けるようになる。
//   上限と一緒に今の値も同じだけ足す。色は HUD の MP バーと同じ青
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline StatItemDef MakeMaxManaUp()
{
    StatItemDef def;

    def.common.id = ItemID::MaxManaUp;
    def.common.name = "Max MP +20";
    def.common.displayName = L"最大MP +20";
    def.common.description = L"MP の上限を上げる。今の MP も同じだけ増える。";
    def.common.category = ItemCategory::Stat;
    def.common.occupyCells = {};                         // 背包に置かないので形は無い
    def.common.influenceCells = {};
    def.common.color = { 0.30f, 0.50f, 0.95f, 1.0f };   // HUD の mpColor
    def.common.iconPath = Res::Icon::MaxManaUp;

    def.kind = StatKind::MaxMana;
    def.amount = 20.0f;
    def.cardLabel = L"最大MP";

    return def;
}
