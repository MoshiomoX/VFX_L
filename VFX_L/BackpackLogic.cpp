// ============================================================
// BackpackLogic.cpp
// ============================================================
#include "BackpackLogic.h"
#include "ItemDatabase.h"

namespace BackpackLogic
{
    // ========================================================
    // 形状の回転
    // (row, col) → (col, -row) を rotation 回繰り返す
    // ========================================================
    std::vector<CellOffset> RotateShape(const std::vector<CellOffset>& shape, int rotation)
    {
        std::vector<CellOffset> out = shape;

        int r = ((rotation % 4) + 4) % 4;   // 負値でも 0~3 に正規化
        for (int i = 0; i < r; ++i)
        {
            for (auto& c : out)
            {
                int nr = c.col;
                int nc = -c.row;
                c.row = nr;
                c.col = nc;
            }
        }
        return out;
    }

    // ========================================================
    // 配置可能判定
    // ========================================================
    bool CanPlace(const BackpackComponent& bp, ItemID id,
        int row, int col, int rotation, int ignoreIndex)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) return false;

        auto cells = RotateShape(c->occupyCells, rotation);

        for (const auto& off : cells)
        {
            int r = row + off.row;
            int cc = col + off.col;

            // 範囲外
            if (r < 0 || r >= BackpackComponent::GRID ||
                cc < 0 || cc >= BackpackComponent::GRID)
                return false;

            // 他のアイテムの占位格と重なる
            int occ = bp.GetOccupant(r, cc);
            if (occ >= 0 && occ != ignoreIndex)
                return false;
        }
        return true;
    }

    // ========================================================
    // 配置
    // ========================================================
    int Place(BackpackComponent& bp, ItemID id, int row, int col, int rotation)
    {
        if (!CanPlace(bp, id, row, col, rotation))
            return -1;

        PlacedItem item;
        item.id = id;
        item.row = row;
        item.col = col;
        item.rotation = ((rotation % 4) + 4) % 4;

        bp.items.push_back(item);
        RebuildOccupancy(bp);
        bp.dirty = true;

        return (int)bp.items.size() - 1;
    }

    // ========================================================
    // 削除
    // ※items から消すとインデックスがずれるので必ず占有表を作り直す
    // ========================================================
    void Remove(BackpackComponent& bp, int itemIndex)
    {
        if (itemIndex < 0 || itemIndex >= (int)bp.items.size()) return;

        bp.items.erase(bp.items.begin() + itemIndex);
        RebuildOccupancy(bp);
        bp.dirty = true;
    }

    // ========================================================
    // 占有表の再構築（items が真データ、占有表は派生データ）
    // ========================================================
    void RebuildOccupancy(BackpackComponent& bp)
    {
        bp.occupancy.fill(-1);

        for (size_t i = 0; i < bp.items.size(); ++i)
        {
            const auto& item = bp.items[i];
            const ItemCommon* c = ItemDatabase::GetCommon(item.id);
            if (!c) continue;

            auto cells = RotateShape(c->occupyCells, item.rotation);
            for (const auto& off : cells)
                bp.SetOccupant(item.row + off.row, item.col + off.col, (int)i);
        }
    }

    // ========================================================
    // そのマスのアイテム
    // ========================================================
    int GetItemAt(const BackpackComponent& bp, int row, int col)
    {
        return bp.GetOccupant(row, col);
    }
}