// ============================================================
// Items/OutletSpells.h
// 出口魔法（2026-10-31、ユーザー：組み合わせの感じが無い → 背包乱斗 × Noita → 「出口を持てる新しい魔法」）。
//   自分も攻撃魔法として自分の間隔で撃つ。出口マス（ItemCommon::outletCells。形と一緒に回る）に別の攻撃魔法が
//   乗っていると、その魔法は自分では撃たなくなり、この魔法の弾が消えた所で発動する。
//   出口の先がまた出口魔法なら続けて繋がる。今ある 15 個の魔法は何も変わらない。
//   3 つの違い = 発動の時（当たった所 / 飛び切った所で 2 つ同時 / 落ちた 2 秒後）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

// 導きの火花：撃つ時に狙いを決める小さな火花。威力は低い。出口は前に 1 つ
inline ProjectileItemDef MakeOutletSpark()
{
    ProjectileItemDef def;

    def.common.id = ItemID::OutletSpark;
    def.common.name = "OutletSpark";
    def.common.displayName = L"導きの火花";
    def.common.description = L"敵を狙って飛ぶ小さな火花。威力は低いが、出口に置いた魔法を当たった所まで運ぶ。";
    def.common.iconPath = Res::Icon::OutletSpark;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = { { 0, 0 }, { 0, 1 } };   // 横 2 マス
    def.common.outletCells = { { 0, 2 } };             // 前（右）に 1 つ
    def.common.outletText = L"当たった所で、出口の魔法が発動する";
    def.common.color = { 1.00f, 0.78f, 0.30f, 1.0f };  // 金

    def.baseStats.id = ItemID::OutletSpark;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.9f;
    def.baseStats.manaCost = 8.0f;

    def.profile = "OutletSpark";   // Assets/Data/ProjectileData/OutletSpark.json（撃つ時に 1 回だけ狙う）
    return def;
}

// 分かれ玉：敵を貫いて飛ぶ玉。飛び切った所で左右の出口が同時に発動する
inline ProjectileItemDef MakeOutletSplit()
{
    ProjectileItemDef def;

    def.common.id = ItemID::OutletSplit;
    def.common.name = "OutletSplit";
    def.common.displayName = L"分かれ玉";
    def.common.description = L"敵を貫いて飛ぶ玉。飛び切った所で、左右の出口に置いた魔法を同時に放つ。";
    def.common.iconPath = Res::Icon::OutletSplit;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 1 } };   // T 字 4 マス
    def.common.outletCells = { { 0, -1 }, { 0, 3 } };                      // 左右に 1 つずつ
    def.common.outletText = L"飛び切った所で、2 つの出口の魔法が同時に発動する";
    def.common.color = { 0.35f, 0.85f, 1.00f, 1.0f };  // 水色

    def.baseStats.id = ItemID::OutletSplit;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 1.6f;
    def.baseStats.manaCost = 12.0f;

    def.profile = "OutletSplit";   // Assets/Data/ProjectileData/OutletSplit.json（直進・貫通）
    return def;
}

// 時限の種：敵の足元へ投げる種。落ちた 2 秒後に出口が発動する
inline ProjectileItemDef MakeOutletSeed()
{
    ProjectileItemDef def;

    def.common.id = ItemID::OutletSeed;
    def.common.name = "OutletSeed";
    def.common.displayName = L"時限の種";
    def.common.description = L"敵の足元へ種を投げる。種は 2 秒後に芽吹き、出口に置いた魔法をその場で放つ。";
    def.common.iconPath = Res::Icon::OutletSeed;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::Rect(2, 2);    // 2x2
    def.common.outletCells = { { 1, 2 } };             // 右下の隣に 1 つ
    def.common.outletDelay = 2.0f;
    def.common.outletText = L"落ちた 2 秒後、その場で出口の魔法が発動する";
    def.common.color = { 0.55f, 0.95f, 0.45f, 1.0f };  // 若葉の緑

    def.baseStats.id = ItemID::OutletSeed;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 2.4f;
    def.baseStats.manaCost = 12.0f;

    def.profile = "OutletSeed";   // Assets/Data/ProjectileData/OutletSeed.json（Lob 型、hitArea = OutletSeedRoot）
    return def;
}

// 浮遊機雷（2026-11-01、Noita の Unstable Crystal With Trigger から）：ゆっくり漂う機雷。敵に触れるか時間が来ると爆発して、出口の魔法が発動する。
// 長く空中に残るので一斉起爆と相性が良い
inline ProjectileItemDef MakeOutletMine()
{
    ProjectileItemDef def;

    def.common.id = ItemID::OutletMine;
    def.common.name = "OutletMine";
    def.common.displayName = L"浮遊機雷";
    def.common.description = L"ゆっくり漂う機雷を放つ。敵に触れると爆発し、出口に置いた魔法をその場で放つ。";
    def.common.iconPath = Res::Icon::OutletMine;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::Rect(2, 2);    // 2x2
    def.common.outletCells = { { 0, 2 } };             // 右上の隣に 1 つ
    def.common.outletText = L"爆発した所で、出口の魔法が発動する";
    def.common.color = { 0.95f, 0.35f, 0.40f, 1.0f };  // 赤

    def.baseStats.id = ItemID::OutletMine;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 1.4f;
    def.baseStats.manaCost = 10.0f;

    def.profile = "OutletMine";   // Assets/Data/ProjectileData/OutletMine.json（直進・遅い、hitArea = Explosion）
    return def;
}
