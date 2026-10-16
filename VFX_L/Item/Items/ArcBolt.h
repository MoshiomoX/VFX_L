// ============================================================
// Items/ArcBolt.h
// アークボルト：一番近い敵へ一瞬で稲妻を走らせる基本魔法（2026-10-15 飛行物から変更）。
//   以前は "ArcOnce" プロファイルで弧を描いて飛ぶ飛行物だった。ユーザー：「投げる球じゃなく、
//   落雷 / レーザーのように一瞬で届いて一度だけ光る物にしたい」→ 魔導光線と同じ仕組み
//   （AreaProfile::Kind::Beam = GPU のカプセル型範囲 + Beam entry の見た目）を「溜め 0・0.12 秒・1 tick」で使う。
//   WeaponSystem の範囲ループが、誘発されない光線型の範囲を「標的が居れば杖先 → 標的へ StartBeam」で撃つ。
//   終点が標的の体で止まり、終点には ArcBoltHit.json（electric-impact）を CPU で再生する。
//
// 設計意図：
//   ・威力 / 太さ / 射程 / 間隔は AreaData/ArcBolt.json 側
//   ・分裂 / 二重は範囲魔法に効かない（AreaParam に回数が無い）。拡大鏡は太さ・射程 ×1.25
//   ・魔導光線の前提（追尾弾 + アークボルト）は今まで通り：稲妻が届いた所が「弾が消えた所」として連鎖する
//     （AreaStats::triggerMask → WeaponSystem の CPU 側の誘発イベント）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline AreaItemDef MakeArcBolt()
{
    AreaItemDef def;

    def.common.id = ItemID::ArcBolt;
    def.common.name = "Arc Bolt";
    def.common.displayName = L"アークボルト";
    def.common.description = L"一番近い敵へ一瞬で稲妻を走らせる。撃つ間隔が短く、消費 MP も少ない。";
    def.common.iconPath = Res::Icon::ArcBolt;
    def.common.category = ItemCategory::Area;
    // 形は強さで決める（2026-10-04）。単体 DPS 約 27（火球と並んで一番高い）→ 4 マスの稲妻の Z 字
    def.common.occupyCells = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 2 } };
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);   // 誘発の届く範囲（形の上下左右。追尾弾と組んで魔導光線を目覚めさせる）
    def.common.color = { 0.40f, 0.80f, 1.00f, 1.0f };   // 水色

    // ---- 基礎値（プロファイル "ArcBolt" があればそちらで上書き）----
    def.baseStats.id = ItemID::ArcBolt;
    def.baseStats.radius = 0.3f;          // 稲妻の半径（太さの半分）
    def.baseStats.duration = 0.12f;       // 一瞬だけ出る
    def.baseStats.tickInterval = 1.0f;    // 持続より長い = 1 回だけ当たる
    def.baseStats.damagePerTick = 10.0f;
    def.baseStats.castInterval = 0.375f;  // 単体の魔法は速め：0.5 → 0.375（2026-09-30）
    def.baseStats.manaCost = 7.5f;        // 間隔と同じ x0.75 で、毎秒の消費は変えない
    def.baseStats.spawnAtTarget = true;   // 標的が要る（一番近い敵へ走る）

    def.vfxId = VFXId::ArcBolt;
    def.profile = "ArcBolt";   // Assets/Data/AreaData/ArcBolt.json（Kind::Beam、溜め 0）

    return def;
}
