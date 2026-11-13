// ============================================================
// Items/IceSpike.h
// アイススパイク：撃った人の足元から一番近い敵の方へ、氷のトゲが扇形（半角 lineFanDeg 度）に広がる魔法。
//
// 設計：
//   ・足元から lineLength m（拡大鏡で伸びる）の扇に、敵の半分くらいの氷のトゲが足元から順に地面から突き出る
//     （見た目は SpellFxSystem → IceSpikeField。地面の高さに 1 本ずつ合わせる）。近い敵が読めない時は「弾が消えた所」を向きにする
//   ・弾は出さない（SpellStats::triggerLine）。判定は扇に並べた GPU の範囲（IceSpikeBurst、WeaponSystem::QueueTriggeredLine）。
//     当たった敵を凍らせる（AreaProfile::freeze。アイスランスの命中と同じ凍結：エリート・Boss は半分、解けた直後は凍らない）。
//     並べた範囲の重なりで 2 回当たらないよう、凍らせる範囲は同じステップでは一番強い 1 つだけ（SwarmAreaDamageCS）
//   ・形は T 字 4 マス
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
    def.common.description = L"足元から一番近い敵の方へ氷のトゲを扇形に広げ、前の敵をまとめて止める。";
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
    def.baseStats.castDelay = 0.3f;       // 二重詠唱の 2 回目（扇を半分ずらして出す）
    def.baseStats.castInterval = 2.0f;    // 誘発のクールダウン
    def.baseStats.manaCost = 18.0f;
    def.baseStats.triggerLine = true;     // 弾を落とさず、足元 → 一番近い敵の向き の線に範囲を並べる
    def.baseStats.lineLength = 12.0f;     // 線の長さ（m）。拡大鏡で x1.25 → 15m
    def.baseStats.lineFanDeg = 22.0f;     // 扇の半角（度）= 44 度の扇に広がる

    def.profile = "IceSpike";   // Assets/Data/ProjectileData/IceSpike.json（弾は出さない。hitArea = 並べる範囲、radius = 拡大鏡の倍率の基準）

    return def;
}