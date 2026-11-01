// ============================================================
// Items/OrbitGuard.h
// 周回弾（2026-11-01、Noita の True Orbit / Magic Guard から）
// プレイヤーの周りを半径 3m で回る光の弾（ProjectileProfile の Orbit 型）。触れた敵を貫くが、貫く度に威力が下がる（黄金の矢と同じ）。
// 遠くの敵には届かない代わりに、囲まれた時に強い基本魔法
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeOrbitGuard()
{
    ProjectileItemDef def;

    def.common.id = ItemID::OrbitGuard;
    def.common.name = "OrbitGuard";
    def.common.displayName = L"周回弾";
    def.common.description = L"自分の周りを回る光の弾を放つ。触れた敵を貫いて傷つける。";
    def.common.iconPath = Res::Icon::OrbitGuard;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::RowLine(2);
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);
    def.common.color = { 0.40f, 0.95f, 0.85f, 1.0f };   // 青緑

    def.baseStats.id = ItemID::OrbitGuard;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 1.2f;
    def.baseStats.manaCost = 9.0f;

    def.profile = "OrbitGuard";   // Assets/Data/ProjectileData/OrbitGuard.json
    return def;
}
