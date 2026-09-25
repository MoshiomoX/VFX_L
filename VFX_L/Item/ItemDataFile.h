// ============================================================
// ItemDataFile.h
// 道具データの保存先（Assets/Data/ItemData/<名前>.json）。
//
// 今入っているのは形（占位格・影響格）だけ。
//   投射物編集器の Item Shapes 頁で道具を選び、マス目を塗って保存した物。
//   ファイルがあれば起動時に Items/*.h の形を上書きし、無ければコードの形のまま。
// 機能（修飾符・弾の profile）は今まで通り Items/*.h のコード。
//
// ファイル名は道具名から空白を抜いた物（"Split Rune" → SplitRune.json）。
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include <string>
#include <vector>

namespace ItemDataFile
{
    inline constexpr const char* kDir = "Assets/Data/ItemData/";

    std::string PathOf(const char* itemName);
    bool Exists(const char* itemName);

    // ファイルがあって読めた時だけ occupy / influence を書き換えて true。
    // 占位格が空のファイルは壊れている扱いで読まない（どこにでも置けて何も塞がない物になるため）
    bool LoadShape(const char* itemName,
        std::vector<CellOffset>& occupy, std::vector<CellOffset>& influence);

    bool SaveShape(const char* itemName,
        const std::vector<CellOffset>& occupy, const std::vector<CellOffset>& influence);
}
