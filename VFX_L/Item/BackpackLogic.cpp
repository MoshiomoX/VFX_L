// ============================================================
// BackpackLogic.cpp
// ============================================================
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"

namespace BackpackLogic
{
    // ========================================================
// ??????????????
//
// ????1???????????????????????????
// ??????????????????????????????
// ========================================================
    std::vector<int> GetItemsOnFrame(const BackpackComponent& bp, int frameIndex)
    {
        std::vector<int> out;
        if (frameIndex < 0 || frameIndex >= (int)bp.frames.size()) return out;

        for (size_t i = 0; i < bp.items.size(); ++i)
        {
            const auto& item = bp.items[i];
            const ItemCommon* c = ItemDatabase::GetCommon(item.id);
            if (!c) continue;

            auto cells = RotateShape(c->occupyCells, item.rotation);
            for (const auto& off : cells)
            {
                if (bp.GetFrameAt(item.row + off.row, item.col + off.col) == frameIndex)
                {
                    out.push_back((int)i);
                    break;
                }
            }
        }
        return out;
    }
    // ========================================================
    // ?????
    // (row, col) ? (col, -row) ? rotation ?????
    // ========================================================
    std::vector<CellOffset> RotateShape(const std::vector<CellOffset>& shape, int rotation)
    {
        std::vector<CellOffset> out = shape;

        int r = ((rotation % 4) + 4) % 4;   // ???? 0~3 ????
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
    // ??:??????
    // ????2??????????????????????????
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

            // ???
            if (r < 0 || r >= BackpackComponent::GRID ||
                cc < 0 || cc >= BackpackComponent::GRID)
                return false;

            // ??????????????????
            if (!bp.IsPlaceable(r, cc))
                return false;

            // ??????????????
            int occ = bp.GetOccupant(r, cc);
            if (occ >= 0 && occ != ignoreIndex)
                return false;
        }
        return true;
    }

    // ========================================================
    // ??:??
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
    // ??:??
    // ?items ???????????????????????????
    // ========================================================
    void Remove(BackpackComponent& bp, int itemIndex)
    {
        if (itemIndex < 0 || itemIndex >= (int)bp.items.size()) return;

        bp.items.erase(bp.items.begin() + itemIndex);
        RebuildOccupancy(bp);
        bp.dirty = true;
    }

    // ========================================================
    // ??:???????(items ???????????????)
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
    // ?????????
    // ========================================================
    int GetItemAt(const BackpackComponent& bp, int row, int col)
    {
        return bp.GetOccupant(row, col);
    }

    // ========================================================
    // ?:??????
    // ???????????????????????????
    //   ????????????????????????????
    // ========================================================
    bool CanPlaceFrame(const BackpackComponent& bp, ItemID id,
        int row, int col, int rotation, int ignoreIndex)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) return false;

        auto cells = RotateShape(c->occupyCells, rotation);

        for (const auto& off : cells)
        {
            int r = row + off.row;
            int cc = col + off.col;

            if (r < 0 || r >= BackpackComponent::GRID ||
                cc < 0 || cc >= BackpackComponent::GRID)
                return false;

            int occ = bp.GetFrameAt(r, cc);
            if (occ >= 0 && occ != ignoreIndex)
                return false;
        }
        return true;
    }

    // ========================================================
    // ?:??
    // ========================================================
    int PlaceFrame(BackpackComponent& bp, ItemID id, int row, int col, int rotation)
    {
        if (!CanPlaceFrame(bp, id, row, col, rotation))
            return -1;

        PlacedItem f;
        f.id = id;
        f.row = row;
        f.col = col;
        f.rotation = ((rotation % 4) + 4) % 4;

        bp.frames.push_back(f);
        RebuildFrameOccupancy(bp);

        // ???????????????????????????
        bp.dirty = true;

        return (int)bp.frames.size() - 1;
    }

    // ========================================================
    // ?:??
    // ????????????????
    //   ????????? ValidateItems ??????
    // ========================================================
    int RemoveFrame(BackpackComponent& bp, int frameIndex)
    {
        if (frameIndex < 0 || frameIndex >= (int)bp.frames.size()) return 0;

        bp.frames.erase(bp.frames.begin() + frameIndex);
        RebuildFrameOccupancy(bp);

        const int evicted = ValidateItems(bp);
        bp.dirty = true;
        return evicted;
    }

    // ========================================================
 // ?:??
 //
 // ??????????????????
 // ??????????????????????????????????
 // ????????????????????????
 // (????????????????????)?
 //
 // ??????????????????????
 // 1???????????????????????????
 // ??????????????????????????????
 // ========================================================
    bool MoveFrame(BackpackComponent& bp, int frameIndex,
        int newRow, int newCol, int newRotation, int* outEvicted)
    {
        if (outEvicted) *outEvicted = 0;
        if (frameIndex < 0 || frameIndex >= (int)bp.frames.size()) return false;

        auto& f = bp.frames[frameIndex];

        // ????????????????
        if (!CanPlaceFrame(bp, f.id, newRow, newCol, newRotation, frameIndex))
            return false;

        // 1. ????????????????????????
        //    ????????????????????????????
        std::vector<int> riders = GetItemsOnFrame(bp, frameIndex);

        const int dr = newRow - f.row;
        const int dc = newCol - f.col;

        // 2. ?????
        f.row = newRow;
        f.col = newCol;
        f.rotation = ((newRotation % 4) + 4) % 4;
        RebuildFrameOccupancy(bp);

        // 3. ??????????????????
        //    ???????????????????????????????
        //    ?????????????????????????
        for (int idx : riders)
        {
            if (idx < 0 || idx >= (int)bp.items.size()) continue;
            bp.items[idx].row += dr;
            bp.items[idx].col += dc;
        }
        RebuildOccupancy(bp);

        // 4. ???????????????????
        //    ??????????????????????????????
        const int evicted = ValidateItems(bp);
        if (outEvicted) *outEvicted = evicted;

        bp.dirty = true;
        return true;
    }
    // ========================================================
    // ?:???????
    // ========================================================
    void RebuildFrameOccupancy(BackpackComponent& bp)
    {
        bp.frameOccupancy.fill(-1);

        for (size_t i = 0; i < bp.frames.size(); ++i)
        {
            const auto& f = bp.frames[i];
            const ItemCommon* c = ItemDatabase::GetCommon(f.id);
            if (!c) continue;

            auto cells = RotateShape(c->occupyCells, f.rotation);
            for (const auto& off : cells)
                bp.SetFrameAt(f.row + off.row, f.col + off.col, (int)i);
        }
    }

    int GetFrameAt(const BackpackComponent& bp, int row, int col)
    {
        return bp.GetFrameAt(row, col);
    }

    // ========================================================
  // ??????
  //
  // ??????????????????????
  // ???? SpellbookComponent ????
  // ????? = ??? - ???????????????
  // ?????????????????????????
  //
  // ??????????:
  //   erase ???????????????????
  //   ??????????????????????????????
  // ========================================================
    int ValidateItems(BackpackComponent& bp)
    {
        int removed = 0;

        for (int i = (int)bp.items.size() - 1; i >= 0; --i)
        {
            const auto& item = bp.items[i];
            const ItemCommon* c = ItemDatabase::GetCommon(item.id);

            bool ok = (c != nullptr);
            if (ok)
            {
                auto cells = RotateShape(c->occupyCells, item.rotation);
                for (const auto& off : cells)
                {
                    const int r = item.row + off.row;
                    const int cc = item.col + off.col;

                    // ???
                    if (r < 0 || r >= BackpackComponent::GRID ||
                        cc < 0 || cc >= BackpackComponent::GRID)
                    {
                        ok = false;
                        break;
                    }

                    // ????????
                    if (!bp.IsPlaceable(r, cc))
                    {
                        ok = false;
                        break;
                    }

                    // ?????????????
                    // ???????????????????????????????
                    // ????????????????????
                    // ?? index ?????????????????
                    const int occ = bp.GetOccupant(r, cc);
                    if (occ >= 0 && occ != i)
                    {
                        ok = false;
                        break;
                    }
                }
            }

            if (!ok)
            {
                bp.items.erase(bp.items.begin() + i);
                ++removed;
            }
        }

        if (removed > 0)
        {
            RebuildOccupancy(bp);
            bp.dirty = true;
        }
        return removed;
    }
    // ========================================================
// ????????????? ID ??
//
// ??????????????????????????
// ???????????????????????????
// ????????????????????????
// ========================================================
    int CountPlaced(const BackpackComponent& bp, ItemID id)
    {
        int n = 0;
        for (const auto& item : bp.items)
            if (item.id == id) ++n;
        return n;
    }

    int CountPlacedFrames(const BackpackComponent& bp, ItemID id)
    {
        int n = 0;
        for (const auto& f : bp.frames)
            if (f.id == id) ++n;
        return n;
    }
}