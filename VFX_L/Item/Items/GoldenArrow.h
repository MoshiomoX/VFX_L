// ============================================================
// Items/GoldenArrow.h
// 黄金の矢：投射物エディタの "GoldenArrow" プロファイルで飛ぶ飛行物型。
//
// 設計意図：
//   ・曲がらない・追わない。撃った瞬間の狙い（最寄りの敵の予測位置）へまっすぐ速く飛び、並んだ敵を貫く
//   ・見た目はエフェクトモデルの弓矢（gonjian.FBX）が金色に光りながら弾と一緒に飛ぶ（GoldenArrow.json）
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
    def.common.description = L"速く飛ぶ金色の矢。並んだ敵をまとめて射抜く。";   // 貫通は GoldenArrow.json の pierce
    def.common.iconPath = Res::Icon::GoldenArrow;
    def.common.category = ItemCategory::Projectile;
    // 縦 3 マスの矢柄
    def.common.occupyCells = ItemShape::ColLine(3);
    def.common.influenceCells = {};
    def.common.color = { 1.00f, 0.78f, 0.25f, 1.0f };   // 金

    // ---- どう撃つか（弾の威力・速さ・見た目は profile 側）----
    def.baseStats.id = ItemID::GoldenArrow;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.525f;  // 単体の魔法は速め
    def.baseStats.manaCost = 6.75f;

    // ---- 飛び方 ----
    def.profile = "GoldenArrow";   // Assets/Data/ProjectileData/GoldenArrow.json（直進）

    return def;
}
