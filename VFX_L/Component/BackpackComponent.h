// ============================================================
// BackpackComponent.h
// グリッドの配置データ（純データ）。
//
// ※二層構造にする理由：
//   異形アイテムは複数マスを占めるため、マス側にアイテムのデータを持たせると
//   同じ情報が複数マスに重複してしまう。
//   よって「アイテム一覧」を真データ、「占有表」は派生データとする。
//
// ※さらに「設置枠」の層を分ける理由：
//   GRID は画布の上限であって、置ける場所ではない。
//   魔法を置けるのは枠が敷かれたマスだけ。
//   枠自体もブロックとして配置するので、どこへ広げるかが構築の選択になる。
//
//   枠と魔法は別々の占有表を持つ。共用にすると
//   「枠の上に魔法を置く」が自分同士の衝突になってしまう。
// ============================================================
#pragma once
#include "SpellID.h"
#include <vector>
#include <array>

// 配置済みアイテム1個ぶん（これが真データ）
struct PlacedItem
{
    ItemID id = ItemID::Unknown;
    int    row = 0;        // アンカーの位置
    int    col = 0;
    int    rotation = 0;   // 0/1/2/3（×90度）
};

struct BackpackComponent
{
    static constexpr int GRID = 7;

    // ---- 魔法ブロック ----
    std::vector<PlacedItem> items;
    std::array<int, GRID* GRID> occupancy;      // items のインデックス、-1 は空

    // ---- 設置枠 ----
    // ※枠も同じ形で持つ。移動・回転・取り外しが魔法と同じ操作でできる。
    std::vector<PlacedItem> frames;
    std::array<int, GRID* GRID> frameOccupancy; // frames のインデックス、-1 は枠なし

    // 集約結果が古くなったか（true なら再計算が必要）
    bool dirty = true;

    BackpackComponent()
    {
        occupancy.fill(-1);
        frameOccupancy.fill(-1);
    }

    // ---- 魔法の占有 ----
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

    // ---- 枠の占有 ----
    int  GetFrameAt(int row, int col) const
    {
        if (row < 0 || row >= GRID || col < 0 || col >= GRID) return -1;
        return frameOccupancy[row * GRID + col];
    }

    void SetFrameAt(int row, int col, int index)
    {
        if (row < 0 || row >= GRID || col < 0 || col >= GRID) return;
        frameOccupancy[row * GRID + col] = index;
    }

    // そのマスに魔法を置けるか（枠が敷かれているか）
    bool IsPlaceable(int row, int col) const { return GetFrameAt(row, col) >= 0; }

    // 枠が敷かれているマスの総数（UI 表示用）
    int PlaceableCount() const
    {
        int n = 0;
        for (int v : frameOccupancy) if (v >= 0) ++n;
        return n;
    }
};