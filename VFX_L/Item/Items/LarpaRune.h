// ============================================================
// LarpaRune.h
// 分身のルーン（2026-11-01、Noita の Larpa Explosion から）
// 光るマスの魔法の弾が消える時（命中・寿命・壁）、そこから四方へ分身を放つ。
// 分身は威力 40%・寿命半分で、もう分身しない。出口の魔法も運ばない（弾 1 発に 1 回分の決まりを崩さないため）。
// Drop / Lob 型（メテオ・毒）と範囲魔法には効かない。WeaponSystem::QueueLarpa
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline FunctionItemDef MakeLarpaRune()
{
    FunctionItemDef def;

    def.common.id = ItemID::LarpaRune;
    def.common.name = "Larpa Rune";
    def.common.displayName = L"分身のルーン";
    def.common.description = L"光るマスの魔法の弾が消える時、周りへ 6 発の分身を放つ。分身の威力は 40%。";
    def.common.category = ItemCategory::Function;
    def.common.occupyCells = { { 0, 0 }, { 0, 1 } };   // 横 2 マス
    def.common.influenceCells = { { 0, -1 }, { 0, 2 } };   // 両端の 1 マスずつ
    def.common.color = { 1.00f, 0.62f, 0.35f, 1.0f };   // 橙
    def.common.iconPath = Res::Icon::LarpaRune;

    def.spellModifiers.push_back({ SpellParam::LarpaCount, ModifyOp::Add,      6.0f });
    def.spellModifiers.push_back({ SpellParam::ManaCost,   ModifyOp::Multiply, 1.3f });

    return def;
}
