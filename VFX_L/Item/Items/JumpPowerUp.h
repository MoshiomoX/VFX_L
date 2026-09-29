// ============================================================
// Items/JumpPowerUp.h
// レベルアップの候補「跳躍力 +8%」（2026-09-28 用户の依頼「跳ぶ速さ」）。
//
// 設計意図：
//   跳んだ瞬間の上向きの速さ（jumpPower）を上げる。高さは速さの 2 乗で伸びるので
//   1 枚で約 +17%。台地や段差を越えやすくなり、滑りから跳んだ時の飛距離も伸びる。
//   掛け算なので重ねると複利
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline StatItemDef MakeJumpPowerUp()
{
    StatItemDef def;

    def.common.id = ItemID::JumpPowerUp;
    def.common.name = "Jump Power +8%";
    def.common.displayName = L"跳躍力 +8%";
    def.common.description = L"跳ぶ勢いを上げる。より高く、より遠くへ跳べる。";
    def.common.category = ItemCategory::Stat;
    def.common.occupyCells = {};                         // 背包に置かないので形は無い
    def.common.influenceCells = {};
    def.common.color = { 0.55f, 0.75f, 1.00f, 1.0f };
    def.common.iconPath = Res::Icon::JumpPowerUp;

    def.kind = StatKind::JumpPower;
    def.amount = 0.08f;
    def.percent = true;
    def.cardLabel = L"跳躍力";

    return def;
}
