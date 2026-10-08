// ============================================================
// Items/SplitRune.h
// 分裂ルーン：機能型。隣接する攻撃ブロックを多発化させる。
//
// 設計意図：
//   ・空間展開の代表。1発を扇状の複数発に変える
//   ・ダメージは分散（×0.6）させるので、総火力は微増程度に留める
//     → 「弾数が増えれば強い」だけにならないようにする
//   ・影響マスは十字四隣。中央に置けば最大4つの攻撃ブロックを強化できる
//     → 「中央は価値が高い」という空間的な駆け引きを作る
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline FunctionItemDef MakeSplitRune()
{
    FunctionItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::SplitRune;
    def.common.name = "Split Rune";
    def.common.displayName = L"分裂のルーン";
    def.common.description = L"隣の魔法の弾を扇状に増やす。一発あたりの威力（落ちた所に残る範囲も）は下がる。";
    def.common.category = ItemCategory::Function;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = ItemShape::Cross();      // 上下左右
    def.common.color = { 0.30f, 0.90f, 0.90f, 1.0f }; // 青緑
    def.common.iconPath = Res::Icon::SplitRune;

    // ---- 飛行物型への修飾（3つ同時に効く）----
    def.spellModifiers.push_back({ SpellParam::ProjectileCount, ModifyOp::Add,      1.0f });
    def.spellModifiers.push_back({ SpellParam::Damage,          ModifyOp::Multiply, 0.6f });
    // 弾が落ちた所・当たった所に出す範囲（毒沼・爆発）も 1 つあたり 0.6 倍（2026-10-08）。
    // これが無いと毒は「毒沼が満額のまま 2 つ」= 2 倍になり、直射の魔法（2 発 × 0.6 = 1.2 倍）との差が広がっていた（spellbench）
    def.spellModifiers.push_back({ SpellParam::AreaDamage,      ModifyOp::Multiply, 0.6f });
    def.spellModifiers.push_back({ SpellParam::SpreadAngle,     ModifyOp::Add,      15.0f });

    // ---- AOE への修飾：範囲を少し広げる ----
    // ※「分裂」は AOE では「範囲拡大」として解釈する
    def.areaModifiers.push_back({ AreaParam::Radius,        ModifyOp::Multiply, 1.25f });
    def.areaModifiers.push_back({ AreaParam::DamagePerTick, ModifyOp::Multiply, 0.8f });

    return def;
}