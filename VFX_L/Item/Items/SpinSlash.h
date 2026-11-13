// ============================================================
// Items/SpinSlash.h
// 回旋斬：範囲魔法（AreaProfile::Kind::Blades、"SpinSlash" プロファイル）。
// 幻の曲刀で自分の周りを一気に一回り斬る（WeaponSystemBlades.cpp）。近くの敵が居る時だけ。
// 見た目は BladeField（Sword493.fbx の曲刀）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline AreaItemDef MakeSpinSlash()
{
    AreaItemDef def;

    def.common.id = ItemID::SpinSlash;
    def.common.name = "SpinSlash";
    def.common.displayName = L"回旋斬";
    def.common.description = L"幻の曲刀で自分の周りを一回り斬り払う。";
    def.common.iconPath = Res::Icon::SpinSlash;
    def.common.category = ItemCategory::Area;
    // 斜めの 3 マス（曲刀の弧）
    def.common.occupyCells = { { 0, 1 }, { 1, 0 }, { 1, 1 } };
    def.common.influenceCells = {};
    def.common.color = { 0.85f, 0.55f, 1.00f, 1.0f };   // 薄紫

    // ---- 基礎値（プロファイル "SpinSlash" があればそちらで上書き）----
    def.baseStats.id = ItemID::SpinSlash;
    def.baseStats.radius = 2.7f;          // 剣先までの距離
    def.baseStats.duration = 0.34f;       // 一回りの秒
    def.baseStats.tickInterval = 1.0e9f;
    def.baseStats.damagePerTick = 30.0f;  // 一回りあたり
    def.baseStats.castInterval = 1.6f;
    def.baseStats.manaCost = 10.0f;
    def.baseStats.spawnAtTarget = false;

    def.profile = "SpinSlash";   // Assets/Data/AreaData/SpinSlash.json
    return def;
}
