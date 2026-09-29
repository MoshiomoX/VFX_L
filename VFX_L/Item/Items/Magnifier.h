// ============================================================
// Items/Magnifier.h
// 拡大鏡：機能型。斜めに隣り合う攻撃ブロックを大きくする。
//
// 設計意図：
//   ・「大きさ」の代表。弾の当たり判定・命中時の爆発の範囲・見た目（粒子・光・連番絵・隕石の警告の輪）が
//     まとめて 1.5 倍になる。見た目は GPU が「当たり半径 / profile の半径」の倍率で拡大する
//     （SwarmSpawnProjCS → SwarmProjScale）ので、ここは当たり半径を変えるだけでよい
//   ・代わりに消費魔力 1.3 倍（2026-09-28 用户決定）
//   ・影響格は斜め 4 マス（X 字）。十字の分裂・二重詠唱と重ならないので、
//     同じ魔法を 2 種のルーンで同時に強化できる = 置き方の駆け引き
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline FunctionItemDef MakeMagnifier()
{
    FunctionItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::Magnifier;
    def.common.name = "Magnifier";
    def.common.displayName = L"拡大鏡";
    def.common.description = L"斜めに隣り合う魔法を大きくする。弾と爆発の範囲が 1.5 倍になるが、消費MPも増える。";
    def.common.category = ItemCategory::Function;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = ItemShape::Diagonal();   // 斜め 4 マス
    def.common.color = { 1.00f, 0.82f, 0.35f, 1.0f };   // 金
    def.common.iconPath = Res::Icon::Magnifier;

    // ---- 飛行物型への修飾 ----
    // 当たり半径を上げると、GPU 側で爆発の範囲と見た目も同じ倍率になる
    def.spellModifiers.push_back({ SpellParam::Radius,   ModifyOp::Multiply, 1.5f });
    def.spellModifiers.push_back({ SpellParam::ManaCost, ModifyOp::Multiply, 1.3f });

    // ---- AOE への修飾：範囲を広げる ----
    def.areaModifiers.push_back({ AreaParam::Radius,   ModifyOp::Multiply, 1.5f });
    def.areaModifiers.push_back({ AreaParam::ManaCost, ModifyOp::Multiply, 1.3f });

    return def;
}
