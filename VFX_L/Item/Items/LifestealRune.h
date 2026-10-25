// ============================================================
// Items/LifestealRune.h
// 吸血のルーン（2026-10-25、ユーザー「バランスのために吸血のモジュールを。その上に置いた魔法でプレイヤーの HP が戻る」→
// 推奨の「ダメージの割合」）：上下左右に隣り合う魔法が敵に与えたダメージの 3% がプレイヤーの HP に戻る。
//   ・戻る量は 1 秒あたり最大 HP の 2% まで（LifestealHeal。群れに撃ち込んでも一瞬で満タンにはならない）
//   ・代価：その魔法の威力 x0.85（着弾の範囲も）
//   ・ルーンを 2 つ付けても割合は増えない（GPU には「吸血の弾 / 範囲」の印だけ渡す。割合は LifestealHeal の 1 つ）
// GPU 側：弾は生成依頼の tag の最上位 bit（Swarm::kTagLifesteal）→ SwarmHitCS が当てた敵の状態に印、
// 範囲は tickNow の Swarm::kAreaExtraLifesteal → SwarmAreaDamageCS が同じ印。SwarmDamageTextCS が印の付いた敵の
// HP の減りを吸血の累計（SwarmSystem::ConsumeLifesteal）へ足す
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline FunctionItemDef MakeLifestealRune()
{
    FunctionItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::LifestealRune;
    def.common.name = "Lifesteal Rune";
    def.common.displayName = L"吸血のルーン";
    def.common.description = L"上下左右の魔法が敵に与えたダメージの一部で、HP が回復する。そのかわり威力は少し下がる。";
    def.common.category = ItemCategory::Function;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = ItemShape::Cross();   // 上下左右 4 マス
    def.common.color = { 0.85f, 0.10f, 0.16f, 1.0f };   // 血の赤
    def.common.iconPath = Res::Icon::LifestealRune;

    // ---- 飛行物型への修飾（着弾で出す範囲の威力にも同じ代価）----
    def.spellModifiers.push_back({ SpellParam::Lifesteal,  ModifyOp::Add,      0.03f });
    def.spellModifiers.push_back({ SpellParam::Damage,     ModifyOp::Multiply, 0.85f });
    def.spellModifiers.push_back({ SpellParam::AreaDamage, ModifyOp::Multiply, 0.85f });

    // ---- AOE への修飾 ----
    def.areaModifiers.push_back({ AreaParam::Lifesteal,     ModifyOp::Add,      0.03f });
    def.areaModifiers.push_back({ AreaParam::DamagePerTick, ModifyOp::Multiply, 0.85f });

    return def;
}
