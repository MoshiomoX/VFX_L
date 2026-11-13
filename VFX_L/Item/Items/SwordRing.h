// ============================================================
// Items/SwordRing.h
// 魔剣の輪：範囲魔法（AreaProfile::Kind::Blades、"SwordRing" プロファイル）。
// 数秒ごとに幻の剣を呼び、プレイヤーの周りを回して、触れた敵を斬る（WeaponSystemBlades.cpp）。
// 1 体の敵は剣 1 本の通過で 1 回だけ当たる。見た目は BladeField（Sword_Big.fbx の直剣）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline AreaItemDef MakeSwordRing()
{
    AreaItemDef def;

    def.common.id = ItemID::SwordRing;
    def.common.name = "SwordRing";
    def.common.displayName = L"魔剣の輪";
    def.common.description = L"幻の剣を呼び、自分の周りを回して近くの敵を斬る。";
    def.common.iconPath = Res::Icon::SwordRing;
    def.common.category = ItemCategory::Area;
    // 2x2 の 4 マス
    def.common.occupyCells = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
    def.common.influenceCells = {};
    def.common.color = { 0.45f, 0.80f, 1.00f, 1.0f };   // 空色

    // ---- 基礎値（プロファイル "SwordRing" があればそちらで上書き）----
    def.baseStats.id = ItemID::SwordRing;
    def.baseStats.radius = 2.4f;          // 剣先までの距離
    def.baseStats.duration = 3.0f;        // 剣が回っている秒
    def.baseStats.tickInterval = 1.0e9f;
    def.baseStats.damagePerTick = 12.0f;  // 剣 1 本の通過あたり
    def.baseStats.castInterval = 4.5f;
    def.baseStats.manaCost = 22.0f;
    def.baseStats.spawnAtTarget = false;

    def.profile = "SwordRing";   // Assets/Data/AreaData/SwordRing.json
    return def;
}
