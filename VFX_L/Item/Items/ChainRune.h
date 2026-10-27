// ============================================================
// ChainRune.h
// 連鎖のルーン（2026-10-16）
// 上下 2 マスの魔法の弾が、敵に当たっても消えずに近くの別の敵へ跳ぶ（最大 2 回）。
// そのかわり 1 発の威力は下がる。GPU 側: SwarmHitCS が当たった弾を生かしたまま
// projStates に「連鎖中」を書き、SwarmProjMoveCS が次の敵へ曲線を組み直す（SwarmCommon.hlsli の SWARM_PROJ_CHAIN*）。
// Drop / Lob 型（メテオ・毒）と範囲魔法には効かない。
// 例外 = アークボルト（2026-10-20、ユーザー：「電弧も跳ねるように」）：溜め 0 の光線型（AreaStats::chainCount）は
// 届いた敵から近くの別の敵へ稲妻をもう一本走らせる（WeaponSystem::UpdateBeams の連鎖、跳ぶ先は SwarmBeamTargetCS の cmd 3）。
// 毒沼・突風・魔導光線にはルーンの代価も掛からない（AreaModifier::chainOnly）
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
    def.common.description = L"光るマスの魔法の弾（アークボルトの稲妻も）が、当たった敵から近くの別の敵へ 2 回まで跳ぶ。そのかわり 1 発の威力は下がる。";
    def.common.category = ItemCategory::Function;
    def.common.occupyCells = { { 0, 0 }, { 1, 0 } };   // 縦 2 マス（2026-10-27）
    def.common.influenceCells = { { -1, 0 }, { 2, 0 } };   // 上下の端の 1 マスずつ
    def.common.color = { 0.72f, 0.55f, 1.00f, 1.0f };   // 稲妻の紫
    def.common.iconPath = Res::Icon::ChainRune;

    // ---- 飛行物型への修飾 ----
    // 跳ぶたびに同じ威力で当たるので、全部当たれば 0.75 × 3 = 2.25 倍。単体の敵には 0.75 倍（群れ向けのルーン）
    def.spellModifiers.push_back({ SpellParam::ChainCount, ModifyOp::Add,      2.0f });
    def.spellModifiers.push_back({ SpellParam::Damage,     ModifyOp::Multiply, 0.7f });

    // ---- AOE への修飾：アークボルト（溜め 0 の光線）だけ。他の範囲魔法には ItemDatabase::ApplyModifier が掛けない ----
    def.areaModifiers.push_back({ AreaParam::ChainCount,    ModifyOp::Add,      2.0f,  true });
    def.areaModifiers.push_back({ AreaParam::DamagePerTick, ModifyOp::Multiply, 0.7f,  true });

    return def;
}
