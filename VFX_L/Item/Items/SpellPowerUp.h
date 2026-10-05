// ============================================================
// Items/SpellPowerUp.h
// レベルアップの候補「魔法威力 +10%」（2026-10-02 ユーザーの依頼「選択肢に魔法の強さを足す」）。
//
// 設計意図：
//   PlayerStatsComponent::spellPower を上げる。全部の攻撃魔法のダメージ（弾の命中・
//   命中 / 着弾で出る範囲（爆発・毒の池）・光線）に掛かる。
//   掛け算なので重ねると複利（1.10 → 1.21 → 1.33 …）。
//   集約（BackpackAggregateSystem）で掛けるので、バックパックの説明の数字も上がった値になる
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline StatItemDef MakeSpellPowerUp()
{
    StatItemDef def;

    def.common.id = ItemID::SpellPowerUp;
    def.common.name = "Spell Power +10%";
    def.common.displayName = L"魔法威力 +10%";
    def.common.description = L"すべての攻撃魔法の威力を上げる。爆発や毒沼、光線にも効果がある。";
    def.common.category = ItemCategory::Stat;
    def.common.occupyCells = {};                         // バックパックに置かないので形は無い
    def.common.influenceCells = {};
    def.common.color = { 1.00f, 0.42f, 0.30f, 1.0f };
    def.common.iconPath = Res::Icon::SpellPowerUp;

    def.kind = StatKind::SpellPower;
    def.amount = 0.10f;
    def.percent = true;
    def.cardLabel = L"魔法威力";

    return def;
}
