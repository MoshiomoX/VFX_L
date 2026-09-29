// ============================================================
// Items/GoldenArrow.h
// 黄金の矢：投射物編集器の "GoldenArrow" プロファイルで飛ぶ飛行物型（2026-09-30 用户の依頼）。
//
// 設計意図：
//   ・曲がらない・追わない。撃った瞬間の狙い（最寄りの敵の予測位置）へまっすぐ速く飛ぶ
//   ・見た目は特効模型の弓矢（gonjian.FBX）が金色に光りながら弾と一緒に飛ぶ（GoldenArrow.json）
//   ・威力 12・速さ 32 m/s・0.525 秒毎・MP 6.75（2026-09-30 に x0.75）。火球（爆発込み）より弱く、追尾弾より少し強い一点型
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeGoldenArrow()
{
    ProjectileItemDef def;

    def.common.id = ItemID::GoldenArrow;
    def.common.name = "Golden Arrow";
    def.common.displayName = L"黄金の矢";
    def.common.description = L"金色に輝く矢を放つ。曲がらずまっすぐ、速く飛ぶ。";
    def.common.iconPath = Res::Icon::GoldenArrow;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::ColLine(2);   // 縦 2 マス（矢柄。2026-09-30）
    def.common.influenceCells = {};
    def.common.color = { 1.00f, 0.78f, 0.25f, 1.0f };   // 金

    // ---- どう撃つか（弾の威力・速さ・見た目は profile 側）----
    def.baseStats.id = ItemID::GoldenArrow;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.525f;  // 単体の魔法は速め：0.7 → 0.525（2026-09-30）
    def.baseStats.manaCost = 6.75f;       // 間隔と同じ x0.75 で、毎秒の消費は変えない

    // ---- 飛び方 ----
    def.profile = "GoldenArrow";   // Assets/Data/ProjectileData/GoldenArrow.json（直進）

    return def;
}
