// ============================================================
// Items/HasteRune.h
// 加速のルーン：機能型。左右に隣り合う攻撃ブロックの発動間隔を縮める。
//
// 設計意図：
//   ・「手数」の代表。発動間隔 x0.75 = 同じ時間で 1.33 倍撃てる
//   ・代償なし（2026-09-30 用户決定）。ただし 1 回の消費 MP は変わらないので、
//     毎秒の消費は 1.33 倍になる = 魔力回復が追いつくかどうかが実質の代償
//   ・影響格は左右 2 マス（横一列の両隣）。十字の分裂・二重詠唱、斜めの拡大鏡と
//     一部しか重ならないので、他のルーンと並べて同じ魔法を強化しやすい
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline FunctionItemDef MakeHasteRune()
{
    FunctionItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::HasteRune;
    def.common.name = "Haste";
    def.common.displayName = L"加速のルーン";
    def.common.description = L"左右に隣り合う魔法を速く撃てるようにする。発動間隔が短くなる。";
    def.common.category = ItemCategory::Function;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = ItemShape::Sides();   // 左右 2 マス
    def.common.color = { 0.55f, 1.00f, 0.40f, 1.0f };   // 若草
    def.common.iconPath = Res::Icon::HasteRune;

    // ---- 飛行物型への修飾 ----
    def.spellModifiers.push_back({ SpellParam::CastInterval, ModifyOp::Multiply, 0.75f });

    // ---- AOE への修飾 ----
    def.areaModifiers.push_back({ AreaParam::CastInterval, ModifyOp::Multiply, 0.75f });

    return def;
}
