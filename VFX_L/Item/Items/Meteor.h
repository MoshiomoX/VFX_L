// ============================================================
// Items/Meteor.h
// 隕石：上級魔法（2026-09-30〜）。"Meteor" プロファイル（Drop 型）。
// 自分では撃たない。ファイアボールと石弾の両方の影響マスが届いている時だけ有効になり、
// どちらかの弾が消えた場所（命中・寿命・壁）へ空から斜めに落ちる（WeaponSystem の誘発）。
// 着弾点で GPU が MeteorBlast（Assets/Data/AreaData/MeteorBlast.json）を出す。
// 落ちる所には警告の輪（SwarmSystem::dropRing）。落ちる途中では敵に当たらない
//
// 設計意図：
//   ・強い範囲攻撃は基本魔法を組み合わせないと使えない = 1 種類だけ育てても届かない
//   ・自分のクールダウンと MP を持つ。クールダウンが明けてから最初に消えた火球・石弾の場所に 1 個落ちる
//   ・直撃は弱く、爆発が本体。爆発の威力・半径は Area プロファイル側（エディタで調整）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeMeteor()
{
    ProjectileItemDef def;

    def.common.id = ItemID::Meteor;
    def.common.name = "Meteor";
    def.common.displayName = L"メテオ";
    def.common.description = L"上級魔法。ファイアボールとストーンショットの両方に隣接させると使えるようになり、どちらかの弾が消えた場所に隕石を落として、周囲の敵をまとめて吹き飛ばす。";
    def.common.iconPath = Res::Icon::Meteor;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = { { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };   // 十字 5 マス（中心 + 上下左右）
    def.common.influenceCells = {};
    def.common.color = { 1.00f, 0.30f, 0.15f, 1.0f };   // 赤橙

    // ---- どう撃つか（弾の威力・速さ・見た目は profile 側）----
    def.baseStats.id = ItemID::Meteor;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 1.8f;    // 誘発のクールダウン
    def.baseStats.manaCost = 20.0f;

    def.profile = "Meteor";   // Assets/Data/ProjectileData/Meteor.json

    // ---- 前提の基本魔法（両方の影響マスが届いている時だけ有効）----
    def.common.triggeredBy = { ItemID::Fireball, ItemID::StoneShot };

    return def;
}
