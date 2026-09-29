// ============================================================
// Items/ManaRegenUp.h
// レベルアップの候補「魔力回復 +20%」（2026-09-29 用户の依頼「魔法の回復量・回復速度」）。
//
// 設計意図：
//   ManaComponent::regen（毎秒の回復）を上げる。ルーンを重ねて消費が増えた構成を支える札。
//   掛け算なので重ねると複利（25 → 30 → 36 …）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline StatItemDef MakeManaRegenUp()
{
    StatItemDef def;

    def.common.id = ItemID::ManaRegenUp;
    def.common.name = "Mana Regen +20%";
    def.common.displayName = L"魔力回復 +20%";
    def.common.description = L"魔力の戻りを早める。消費の重い構成でも撃ち続けられる。";
    def.common.category = ItemCategory::Stat;
    def.common.occupyCells = {};                         // 背包に置かないので形は無い
    def.common.influenceCells = {};
    def.common.color = { 0.45f, 0.55f, 1.00f, 1.0f };
    def.common.iconPath = Res::Icon::ManaRegenUp;

    def.kind = StatKind::ManaRegen;
    def.amount = 0.20f;
    def.percent = true;
    def.cardLabel = L"魔力回復";

    return def;
}
