// ============================================================
// Items/DoubleCastRune.h
// 二重釈放符：機能型。隣接する攻撃ブロックの発射回数を増やす。
//
// 設計意図：
//   ・時間展開の代表。分裂（空間）と対になる
//   ・ダメージは分散しないので純粋な火力増だが、
//     総マナ消費が castCount 倍になるため持続力を犠牲にする
//     → 「瞬間火力 vs 継続火力」の駆け引きを作る
//   ・影響格は縦横一直線ではなく十字四隣（分裂符と同じ）
// ============================================================
#pragma once
#include "ItemTypes.h"

inline FunctionItemDef MakeDoubleCastRune()
{
    FunctionItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::DoubleCastRune;
    def.common.name = "Double Cast";
    def.common.category = ItemCategory::Function;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = ItemShape::Cross();
    def.common.color = { 0.80f, 0.40f, 1.00f, 1.0f }; // 紫
    def.common.iconPath = nullptr;

    // ---- 飛行物型への修飾 ----
    // ※マナ消費は WeaponSystem 側で manaCost × castCount として扱われるため、
    //   ここで manaCost を上げる必要はない
    def.spellModifiers.push_back({ SpellParam::CastCount,    ModifyOp::Add,      1.0f });
    def.spellModifiers.push_back({ SpellParam::CastInterval, ModifyOp::Multiply, 1.15f });

    // ---- AOE への修飾：持続時間を延ばす ----
    def.areaModifiers.push_back({ AreaParam::Duration, ModifyOp::Multiply, 1.5f });
    def.areaModifiers.push_back({ AreaParam::ManaCost, ModifyOp::Multiply, 1.6f });

    return def;
}