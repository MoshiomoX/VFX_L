// ============================================================
// Items/MaxHealthUp.h
// レベルアップの候補「生命の上限 +20」。
//
// 設計意図：
//   魔法を増やす以外の伸ばし方を三択に混ぜる。
//   押されている時に「攻めを増やすか、耐える余裕を買うか」を選べるようにする。
//   上限と一緒に今の値も同じだけ足す（取った瞬間に効いたと分かるように）。
//   色は HUD の HP バーと同じ赤
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline StatItemDef MakeMaxHealthUp()
{
    StatItemDef def;

    def.common.id = ItemID::MaxHealthUp;
    def.common.name = "Max HP +20";
    def.common.displayName = L"最大HP +20";
    def.common.description = L"HP の上限を上げる。今の HP も同じだけ回復する。";
    def.common.category = ItemCategory::Stat;
    def.common.occupyCells = {};                         // 背包に置かないので形は無い
    def.common.influenceCells = {};
    def.common.color = { 0.85f, 0.25f, 0.25f, 1.0f };   // HUD の hpColor
    def.common.iconPath = Res::Icon::MaxHealthUp;

    def.kind = StatKind::MaxHealth;
    def.amount = 20.0f;
    def.cardLabel = L"最大HP";

    return def;
}
