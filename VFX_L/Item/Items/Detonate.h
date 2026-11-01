// ============================================================
// Items/Detonate.h
// 一斉起爆（2026-11-01、Noita の Spells To X から）
// 空中にある自分の弾を全部、その場で爆発させる範囲魔法（AreaProfile::Kind::Detonate）。
// 弾が 3 発以上飛び続けて 0.4 秒経ったら撃つ（すぐ撃つと手元で爆発する）。弾が消えた扱いになるので、出口の魔法が入った弾はそこで中身も発動する。
// 爆発は SwarmProjMoveCS が 1 ステップだけ全部の弾に出す（SwarmSystem::DetonateProjectiles）。
// 爆発の威力は GPU の範囲定義のまま（ルーンの半径は効かない。弾の範囲の倍率は掛かる）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline AreaItemDef MakeDetonate()
{
    AreaItemDef def;

    def.common.id = ItemID::Detonate;
    def.common.name = "Detonate";
    def.common.displayName = L"一斉起爆";
    def.common.description = L"空中にある自分の弾を全部、その場で爆発させる。出口に魔法を入れた弾は、そこで中身も発動する。";
    def.common.iconPath = Res::Icon::Detonate;
    def.common.category = ItemCategory::Area;
    def.common.occupyCells = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 1 } };   // T 字 4 マス
    def.common.influenceCells = {};
    def.common.color = { 1.00f, 0.45f, 0.30f, 1.0f };

    // ---- 基礎値（プロファイル "Detonate" があればそちらで上書き）----
    def.baseStats.id = ItemID::Detonate;
    def.baseStats.radius = 2.5f;
    def.baseStats.duration = 0.25f;
    def.baseStats.tickInterval = 0.25f;
    def.baseStats.damagePerTick = 14.0f;
    def.baseStats.castInterval = 3.0f;
    def.baseStats.manaCost = 18.0f;
    def.baseStats.spawnAtTarget = false;

    def.vfxId = VFXId::Explosion;
    def.profile = "Detonate";   // Assets/Data/AreaData/Detonate.json

    return def;
}
