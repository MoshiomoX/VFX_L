// ============================================================
// Items/VampireBats.h
// 吸血コウモリ：範囲魔法（AreaProfile::Kind::Bats、"VampireBats" プロファイル）。
// 蝙蝠を放ち、不規則に飛んで一番近い敵を噛み、プレイヤーの所へ戻って噛んだ威力の一部を HP に返す
// （WeaponSystemBats.cpp）。見た目は BatField（コードで作った低ポリの蝙蝠）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline AreaItemDef MakeVampireBats()
{
    AreaItemDef def;

    def.common.id = ItemID::VampireBats;
    def.common.name = "VampireBats";
    def.common.displayName = L"吸血コウモリ";
    def.common.description = L"蝙蝠の群れを放つ。敵を噛んだ蝙蝠は戻ってきて HP を回復する。";
    def.common.iconPath = Res::Icon::VampireBats;
    def.common.category = ItemCategory::Area;
    // 広げた翼と体の 4 マス（上の横 3 + 真ん中の下 1）
    def.common.occupyCells = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 1 } };
    def.common.influenceCells = {};
    def.common.color = { 0.85f, 0.18f, 0.32f, 1.0f };   // 血の赤

    // ---- 基礎値（プロファイル "VampireBats" があればそちらで上書き）----
    def.baseStats.id = ItemID::VampireBats;
    def.baseStats.radius = 0.9f;          // 噛む範囲
    def.baseStats.duration = 4.0f;        // 1 匹の最長の寿命
    def.baseStats.tickInterval = 1.0e9f;
    def.baseStats.damagePerTick = 14.0f;  // 1 匹が 1 回噛む威力
    def.baseStats.castInterval = 2.2f;
    def.baseStats.manaCost = 16.0f;
    def.baseStats.spawnAtTarget = false;

    def.profile = "VampireBats";   // Assets/Data/AreaData/VampireBats.json
    return def;
}
