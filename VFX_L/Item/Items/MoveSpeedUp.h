// ============================================================
// Items/MoveSpeedUp.h
// レベルアップの候補「移動速度 +8%」（2026-09-28 用户の依頼）。
//
// 設計意図：
//   逃げ回る力を伸ばす選択肢。群れに囲まれにくくなり、経験値の玉も拾いやすい。
//   掛け算なので重ねるほど少しずつ大きく効く（1.08 の n 乗）。
//   滑りの初速（moveSpeed × slideBoost）も一緒に上がる
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline StatItemDef MakeMoveSpeedUp()
{
    StatItemDef def;

    def.common.id = ItemID::MoveSpeedUp;
    def.common.name = "Move Speed +8%";
    def.common.displayName = L"移動速度 +8%";
    def.common.description = L"走る速さを上げる。滑り出しの勢いも同じだけ強くなる。";
    def.common.category = ItemCategory::Stat;
    def.common.occupyCells = {};                         // 背包に置かないので形は無い
    def.common.influenceCells = {};
    def.common.color = { 0.45f, 0.85f, 0.55f, 1.0f };
    def.common.iconPath = Res::Icon::MoveSpeedUp;

    def.kind = StatKind::MoveSpeed;
    def.amount = 0.08f;
    def.percent = true;
    def.cardLabel = L"移動速度";

    return def;
}
