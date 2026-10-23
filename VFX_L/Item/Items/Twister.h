// ============================================================
// Items/Twister.h
// 竜巻：上級の範囲魔法（2026-10-22）。"Twister" プロファイル（AreaProfile::Kind::Roam）。
// ユーザー：渦を「下級魔法」にして新しい上級の竜巻を。前提は渦（VoidVortex）+ 突風（Gust）、動きは推奨の「敵の方へ進んで巻き込む」。
// 自分では撃たない。渦と突風の両方の影響マスが届いている時だけ有効になり、
// 渦の弾が消えた所（渦が開く所）か突風の吹いた所に竜巻を起こす（WeaponSystem::StartTwister）。
//   竜巻はプレイヤーに一番近い敵の方へ roamSpeed m/s で進みながら、周りの敵を中心へ引き寄せ（pull）、
//   少し遅くし、tickInterval ごとに傷つける。duration 秒で消える（WeaponSystemTwister.cpp）
// 見た目は Assets/Data/VFXData/Twister.json：特効モデル longjuanfeng2.FBX（上下が広く中ほどが細いねじれた柱。
//   同日ユーザー：「特効モデルの実体の竜巻を元に、ノイズの煙を足す」）を外幕 + 内芯の 2 枚、雲ノイズを上へ流して溶かし、速く回す。
//   周りに屈折の付いた薄紫の煙団（ノイズの煙）、足元に巻き上がる草屑と土粒 + 局所の風源（草・落ち葉が巻かれる）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline AreaItemDef MakeTwister()
{
    AreaItemDef def;

    def.common.id = ItemID::Twister;
    def.common.name = "Twister";
    def.common.displayName = L"竜巻";
    def.common.description = L"敵を巻き込みながら進む竜巻を起こす。";
    def.common.iconPath = Res::Icon::Twister;
    def.common.category = ItemCategory::Area;
    // 漏斗の形 5 マス（上の横 3 + 真ん中から下へ 2）
    def.common.occupyCells = { { 0, -1 }, { 0, 0 }, { 0, 1 }, { 1, 0 }, { 2, 0 } };
    def.common.influenceCells = {};
    def.common.color = { 0.78f, 0.72f, 1.00f, 1.0f };   // 薄紫（白寄り）

    // ---- 基礎値（プロファイル "Twister" があればそちらで上書き）----
    def.baseStats.id = ItemID::Twister;
    def.baseStats.radius = 2.8f;          // 巻き込む半径
    def.baseStats.duration = 4.0f;        // 竜巻が残る秒数
    def.baseStats.tickInterval = 0.5f;
    def.baseStats.damagePerTick = 10.0f;  // 20 dps、巻き込まれ続けた 1 体で約 80
    def.baseStats.castInterval = 6.0f;    // 誘発のクールダウン
    def.baseStats.manaCost = 30.0f;
    def.baseStats.spawnAtTarget = false;  // 誘発された所に出る

    def.vfxId = VFXId::Twister;
    def.profile = "Twister";   // Assets/Data/AreaData/Twister.json

    // ---- 前提の基本魔法（両方の影響マスが届いている時だけ有効）----
    def.common.triggeredBy = { ItemID::VoidVortex, ItemID::Gust };

    return def;
}
