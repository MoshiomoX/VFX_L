// ============================================================
// Items/Magnifier.h
// 拡大鏡：機能型。斜めに隣り合う攻撃ブロックを大きくする。
//
// 設計意図：
//   ・「大きさ」の代表。弾の当たり判定・命中時の爆発の範囲・見た目（粒子・光・連番画像・隕石の警告の輪）が
//     まとめて 1.25 倍になる（2026-10-08 まで 1.5 倍）。見た目は GPU が「当たり半径 / profile の半径」の倍率で拡大する
//     （SwarmSpawnProjCS → SwarmProjScale）ので、ここは当たり半径を変えるだけでよい
//   ・弾の威力 1.15 倍（2026-10-08。単体の魔法にも得がある様に）
//   ・代わりに消費魔力 1.2 倍（2026-09-28 ユーザー決定の 1.3 倍から 10-08 に下げた）
//   ・影響マスは斜め 4 マス（X 字）。十字の分裂・二重詠唱と重ならないので、
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
    def.common.description = L"光るマスの魔法の弾と範囲を大きくし、威力も少し上げる。MP は多めに使う。";
    def.common.category = ItemCategory::Function;
    def.common.occupyCells = { { 0, 0 }, { 0, 1 } };   // 横 2 マス（2026-10-27）
    def.common.influenceCells = { { -1, -1 }, { 1, 2 } };   // 対角の 2 隅（斜め 4 → 2）
    def.common.color = { 1.00f, 0.82f, 0.35f, 1.0f };   // 金
    def.common.iconPath = Res::Icon::Magnifier;

    // ---- 飛行物型への修飾 ----
    // 当たり半径を上げると、GPU 側で爆発の範囲と見た目も同じ倍率になる
    // 2026-10-08：半径 1.5 → 1.25 倍（面積 2.25 → 1.56 倍）・消費 1.3 → 1.2 倍、弾の威力 1.15 倍を足した。
    // 以前は単体の魔法には何も足さず（当たり判定が大きくなるだけ）、毒沼だけが群れに 1.6 倍だった（spellbench）
    def.spellModifiers.push_back({ SpellParam::Radius,   ModifyOp::Multiply, 1.2f });
    def.spellModifiers.push_back({ SpellParam::Damage,   ModifyOp::Multiply, 1.1f });   // 弾そのもの（落ちた所の範囲には効かない）
    def.spellModifiers.push_back({ SpellParam::ManaCost, ModifyOp::Multiply, 1.25f });

    // ---- AOE への修飾：範囲を広げる ----
    // 2026-10-13 ユーザー：「魔導光線に拡大鏡がほとんど効かない」→ 光線は 18m の線なので太さ 0.3m の差では
    // 当たる敵がほぼ変わらない。射程も同じ倍率で伸ばす（18 → 22.5m）。Length は光線以外では 0 のまま
    def.areaModifiers.push_back({ AreaParam::Radius,   ModifyOp::Multiply, 1.2f });
    def.areaModifiers.push_back({ AreaParam::Length,   ModifyOp::Multiply, 1.2f });
    def.areaModifiers.push_back({ AreaParam::ManaCost, ModifyOp::Multiply, 1.25f });

    return def;
}
