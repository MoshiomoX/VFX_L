// ============================================================
// Items/StoneShot.h
// 石弾：基本魔法（2026-09-30）。重い岩を山なりに投げる単体攻撃。
//
// 設計意図：
//   ・ファイアボールと対になる基本魔法。遅く・重く・一発が大きい
//     （profile "StoneShot"：曲線 1 回捕捉で上へ膨らむ = 山なり、威力 22、13 m/s）
//   ・影響マス（上下左右）が届いている上級魔法を誘発する：
//     この弾が消えた場所にメテオが落ちる（メテオにはファイアボールも要る）
//   ・見た目は Rock_2.fbx の岩が弾と一緒に飛ぶ（Mesh 粒子 + 発射源の速度を継承）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeStoneShot()
{
    ProjectileItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::StoneShot;
    def.common.name = "Stone Shot";
    def.common.displayName = L"ストーンショット";
    def.common.description = L"重い岩を山なりに投げる。弾は遅いが一撃が重い。上下左右に隣接する上級魔法を、岩が落ちた場所で発動させる。";
    def.common.iconPath = Res::Icon::StoneShot;
    def.common.category = ItemCategory::Projectile;
    // 形は強さで決める（2026-10-04）。単体 DPS 約 24（一撃が重く当たり判定も大きめ）→ 3 マスの L 字の岩
    def.common.occupyCells = { { 0, 0 }, { 1, 0 }, { 1, 1 } };
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);   // 誘発の届く範囲（形の上下左右）
    def.common.color = { 0.70f, 0.60f, 0.45f, 1.0f };   // 砂岩

    // ---- どう撃つか ----
    def.baseStats.id = ItemID::StoneShot;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.9f;
    def.baseStats.manaCost = 9.0f;

    // ---- 何を撃つか ----
    def.profile = "StoneShot";   // Assets/Data/ProjectileData/StoneShot.json

    return def;
}
