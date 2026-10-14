// ============================================================
// Items/IceSpike.h
// アイススパイク：上級魔法（2026-10-10、ユーザー：「氷と岩でしか発動しない氷のトゲ。ライブラリのモデルで大きな氷のトゲを出す」
//   →「エルデンリングの霜踏みみたいに。ただし大きさは敵の半分くらい」→「そんなに密にしないで、前へ 7〜8 本で十分」
//   →（同日）「本当は撃ち出す形。足元から狙った敵の少し奥まで、出ている間トゲが左右交互に。もう少し多く」）。
//
// 設計：
//   ・自分では撃たない。アイスランスとストーンショットの両方が上下左右に隣り合っている時だけ、どちらかの弾が消えた所で発動する
//   ・発動すると、撃った人の足元から「弾が消えた所の lineOvershoot 先」まで、敵の半分くらいの氷のトゲが左右交互に、
//     足元から順に地面から突き出る（見た目は SpellFxSystem → IceSpikeField。地面の高さに 1 本ずつ合わせる）
//   ・弾は出さない（SpellStats::triggerLine）。判定は線に沿って並べた GPU の範囲（IceSpikeBurst、WeaponSystem::QueueTriggeredLine）。
//     当たった敵を凍らせる（AreaProfile::freeze。アイスランスの命中と同じ凍結：エリート・Boss は半分、解けた直後は凍らない）。
//     並べた範囲の重なりで 2 回当たらないよう、凍らせる範囲は同じステップでは一番強い 1 つだけ（SwarmAreaDamageCS）
//   ・形は強さで決める（2026-10-04 の決まり）：メテオ（十字 5）より少し弱い → T 字 4 マス（ユーザーが選んだ）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeIceSpike()
{
    ProjectileItemDef def;

    def.common.id = ItemID::IceSpike;
    def.common.name = "Ice Spike";
    def.common.displayName = L"アイススパイク";
    def.common.description = L"足元から敵の奥まで氷のトゲを走らせ、通り道の敵をまとめて止める。";
    def.common.iconPath = Res::Icon::IceSpike;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = { { 0, -1 }, { 0, 0 }, { 0, 1 }, { 1, 0 } };   // T 字 4 マス（上の横棒 + 真ん中の下）
    def.common.influenceCells = {};
    def.common.color = { 0.62f, 0.90f, 1.00f, 1.0f };   // 薄い氷の水色（アイスランスより白寄り）

    // ---- どう撃つか（範囲の威力・半径・凍結は IceSpikeBurst 側）----
    def.baseStats.id = ItemID::IceSpike;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.15f;
    def.baseStats.castInterval = 2.0f;    // 誘発のクールダウン
    def.baseStats.manaCost = 18.0f;
    def.baseStats.triggerLine = true;     // 弾を落とさず、足元 → 弾が消えた所の先 の線に範囲を並べる
    def.baseStats.lineOvershoot = 1.5f;   // 弾が消えた所（狙った敵）から奥へ

    def.profile = "IceSpike";   // Assets/Data/ProjectileData/IceSpike.json（弾は出さない。hitArea = 並べる範囲、radius = 拡大鏡の倍率の基準）

    // ---- 前提の基本魔法（両方の影響マスが届いている時だけ有効）----
    def.common.triggeredBy = { ItemID::IceLance, ItemID::StoneShot };

    return def;
}