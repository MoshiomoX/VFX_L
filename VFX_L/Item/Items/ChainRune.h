// ============================================================
// ChainRune.h
// 連鎖のルーン（2026-10-16）
// 上下 2 マスの魔法の弾が、敵に当たっても消えずに近くの別の敵へ跳ぶ（最大 2 回）。
// そのかわり 1 発の威力は下がる。GPU 側: SwarmHitCS が当たった弾を生かしたまま
// projStates に「連鎖中」を書き、SwarmProjMoveCS が次の敵へ曲線を組み直す（SwarmCommon.hlsli の SWARM_PROJ_CHAIN*）。
// Drop / Lob 型（メテオ・毒）と範囲魔法には効かない
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline FunctionItemDef MakeChainRune()
{
    FunctionItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::ChainRune;
    def.common.name = "Chain Rune";
    def.common.displayName = L"連鎖のルーン";
    def.common.description = L"上下の魔法の弾が、当たった敵から近くの別の敵へ 2 回まで跳ぶ。そのかわり 1 発の威力は下がる。";
    def.common.category = ItemCategory::Function;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = ItemShape::Vertical();   // 上下 2 マス（分裂・二重の十字、拡大鏡の斜め、加速の左右と重ならない）
    def.common.color = { 0.72f, 0.55f, 1.00f, 1.0f };   // 稲妻の紫
    def.common.iconPath = Res::Icon::ChainRune;

    // ---- 飛行物型への修飾 ----
    // 跳ぶたびに同じ威力で当たるので、全部当たれば 0.75 × 3 = 2.25 倍。単体の敵には 0.75 倍（群れ向けのルーン）
    def.spellModifiers.push_back({ SpellParam::ChainCount, ModifyOp::Add,      2.0f });
    def.spellModifiers.push_back({ SpellParam::Damage,     ModifyOp::Multiply, 0.75f });

    // ---- AOE への修飾：無し（範囲魔法は跳ばない）----

    return def;
}
