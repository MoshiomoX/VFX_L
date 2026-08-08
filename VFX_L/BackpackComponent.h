// ============================================================
// BackpackComponent.h
// 7x7 グリッドの配置データ（純データ）。
//
// ★二層構造にする理由：
//   異形アイテムは複数マスを占めるため、マス側にアイテムのデータを持たせると
//   同じ情報が複数マスに重複してしまう。
//   よって「アイテム一覧」を真データ、「占有表」は派生データとする。
// ============================================================
#pragma once
#include "SpellID.h"
#include <vector>
#include <array>

// 配置済みアイテム1個ぶん（これが真データ）
struct PlacedItem
{
    ItemID id;
    int    row = 0;        // アンカーの位置
    int    col = 0;
    int    rotation = 0;   // 0/1/2/3（×90度）
};

struct BackpackComponent
{
    static constexpr int GRID = 7;

    // 配置済みアイテム一覧
    std::vector<PlacedItem> items;

    // 占有表（派生データ）。値は items のインデックス、-1 は空。
    // ※items を変更したら RebuildOccupancy で作り直す
    std::array<int, GRID* GRID> occupancy;

    // 集約結果が古くなったか（true なら再計算が必要）
    bool dirty = true;

    BackpackComponent()
    {
        occupancy.fill(-1);
    }

    int  GetOccupant(int row, int col) const
    {
        if (row < 0 || row >= GRID || col < 0 || col >= GRID) return -1;
        return occupancy[row * GRID + col];
    }

    void SetOccupant(int row, int col, int index)
    {
        if (row < 0 || row >= GRID || col < 0 || col >= GRID) return;
        occupancy[row * GRID + col] = index;
    }
};