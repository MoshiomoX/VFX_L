// ============================================================
// Items/Frame3x3.h
// 3x3 の設置枠：初期装備。
//
// 設計意図：
//   1. 開始時点で最低限の作業領域を与える。
//      枠がゼロだと、最初の三択で枠を引くまで何もできない
//   2. 3x3 は「十字の影響マスが中央から4方向すべてに届く」最小の広さ。
//      分裂ルーンを中央に置く戦術が最初から成立する
//   3. アンカーは左上（ItemShape::Rect の仕様）。
//      中央に置くなら 9x9 では (3,3) を指定して 3..5 行 3..5 列を覆う
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline FrameItemDef MakeFrame3x3()
{
    FrameItemDef def;

    def.common.id = ItemID::Frame3x3;
    def.common.name = "Frame 3x3";
    def.common.displayName = L"拡張枠 3x3";
    def.common.description = L"魔法を置ける場所を 3x3 マス広げる。魔法は枠の上にしか置けない。";
    def.common.category = ItemCategory::Frame;
    def.common.occupyCells = ItemShape::Rect(3, 3);
    def.common.influenceCells = {};                        // 枠は何にも影響しない
    def.common.color = { 0.55f, 0.75f, 0.95f, 1.0f };   // 淡い青
    def.common.iconPath = Res::Icon::Frame3x3;

    return def;
}