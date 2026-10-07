// ============================================================
// Items/ShieldUp.h
// レベルアップの候補「最大シールド +25」（2026-10-07 ユーザー指定：シールドは Megabonk と同じ）。
//
// 設計意図：
//   ShieldComponent::max を上げる（Megabonk のシールドの書と同じ 1 回 +25）。
//   HP と同じく、上限と一緒に今の値も同じだけ足す（取った瞬間に効いたと分かるように）。
//   最大HP との違い：自然回復とは別に、5 秒被弾しなければ満タンに戻る。
//   一撃はシールドで止まるので、少しでも残っていれば即死を防げる。
//   色は HUD のシールドのバーと同じ水色
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline StatItemDef MakeShieldUp()
{
    StatItemDef def;

    def.common.id = ItemID::ShieldUp;
    def.common.name = "Max Shield +25";
    def.common.displayName = L"最大シールド +25";
    def.common.description = L"HP より先にダメージを受けるシールドの上限を上げる。"
        L"5 秒間ダメージを受けなければ満タンまで回復する。"
        L"シールドが残っていれば、その一撃で HP は減らない。";
    def.common.category = ItemCategory::Stat;
    def.common.occupyCells = {};                         // バックパックに置かないので形は無い
    def.common.influenceCells = {};
    def.common.color = { 0.35f, 0.75f, 1.00f, 1.0f };
    def.common.iconPath = Res::Icon::ShieldUp;

    def.kind = StatKind::Shield;
    def.amount = 25.0f;
    def.cardLabel = L"シールド";

    return def;
}
