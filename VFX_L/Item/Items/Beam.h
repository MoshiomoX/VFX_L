// ============================================================
// Items/Beam.h
// 魔導光線：上級の範囲魔法（2026-09-30〜）。"Beam" プロファイル（AreaProfile::Kind::Beam）。
// 自分では撃たない。追尾弾（HomingBolt）と弧（ArcBolt）の両方の影響マスが届いている時だけ有効になり、
// どちらかの弾が消えた場所（命中・寿命・壁）の方向へ、プレイヤーの手から光線を撃つ（WeaponSystem::UpdateBeams）。
//   溜め（chargeTime）→ 光線（duration の間、tickInterval ごとにダメージ）→ 根元から縮んで消える
// 判定は GPU のカプセル型範囲（Swarm::kAreaCapsule。起点は毎フレームプレイヤーに付く、終点は地形で切れる）、
// 見た目は Assets/Data/VFXData/Beam.json（Beam entry + 溜めの光球 + 点光源）。
//
// 設計意図：
//   ・貫通する持続ダメージは強いので、基本魔法を 2 種類組み合わせないと使えない（隕石と同じ思想）
//   ・自分のクールダウンと MP を持つ。クールダウンが明けてから最初に消えた追尾弾・弧の方向に 1 本
//   ・威力・太さ・射程・溜めは Area プロファイル側（投射物エディタの Area 頁で調整）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline AreaItemDef MakeBeam()
{
    AreaItemDef def;

    def.common.id = ItemID::Beam;
    def.common.name = "Beam";
    def.common.displayName = L"魔導光線";
    def.common.description = L"上級魔法。ホーミングボルトとアークボルトの両方に隣接させると使えるようになり、どちらかの弾が消えた方向へ、敵を貫通する光線を放つ。";
    def.common.iconPath = Res::Icon::Beam;
    def.common.category = ItemCategory::Area;
    def.common.occupyCells = { { 0, 0 }, { 0, -1 }, { 0, 1 } };   // 横 3 マス（中心 + 左右）
    def.common.influenceCells = {};
    def.common.color = { 0.35f, 0.80f, 1.00f, 1.0f };   // 青白

    // ---- 基礎値（プロファイル "Beam" があればそちらで上書き）----
    def.baseStats.id = ItemID::Beam;
    def.baseStats.radius = 0.6f;          // 光線の半径（太さの半分）
    def.baseStats.duration = 1.2f;        // 光線が出ている秒数
    def.baseStats.tickInterval = 0.1f;
    def.baseStats.damagePerTick = 6.0f;   // 60 dps、1 本で約 72
    def.baseStats.castInterval = 3.0f;    // 誘発のクールダウン
    def.baseStats.manaCost = 30.0f;
    def.baseStats.spawnAtTarget = false;  // プレイヤーの位置（手）から

    def.vfxId = VFXId::Beam;
    def.profile = "Beam";   // Assets/Data/AreaData/Beam.json

    // ---- 前提の基本魔法（両方の影響マスが届いている時だけ有効）----
    def.common.triggeredBy = { ItemID::HomingBolt, ItemID::ArcBolt };

    return def;
}
