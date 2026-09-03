// ============================================================
// BackpackLogic.cpp
// ============================================================
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"

namespace BackpackLogic
{
    // ========================================================
    // 枠の上に乗っている魔法を集める
    //
    // 占位格の1つでもその枠に乗っていれば対象。
    // 複数の枠にまたがる魔法は、どちらの枠からも拾われる。
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
    // 形状の回転
    // (row, col) → (col, -row) を rotation 回繰り返す
    // ========================================================
    std::vector<CellOffset> RotateShape(const std::vector<CellOffset>& shape, int rotation)
    {
        std::vector<CellOffset> out = shape;

        int r = ((rotation % 4) + 4) % 4;   // 負数でも 0~3 に収める
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
    // 魔法: 置けるか
    // 画布内、枠の上、他の魔法と重ならない、の3つを全部満たす時だけ true
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

            // 画布外
            if (r < 0 || r >= BackpackComponent::GRID ||
                cc < 0 || cc >= BackpackComponent::GRID)
                return false;

            // 枠が敷かれていないマスには置けない
            if (!bp.IsPlaceable(r, cc))
                return false;

            // 他の魔法と重なっている
            int occ = bp.GetOccupant(r, cc);
            if (occ >= 0 && occ != ignoreIndex)
                return false;
        }
        return true;
    }

    // ========================================================
    // 魔法: 置く
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
    // 魔法: 消す
    // ※items の index が詰まるので、占有表は必ず組み直す
    // ========================================================
    void Remove(BackpackComponent& bp, int itemIndex)
    {
        if (itemIndex < 0 || itemIndex >= (int)bp.items.size()) return;

        bp.items.erase(bp.items.begin() + itemIndex);
        RebuildOccupancy(bp);
        bp.dirty = true;
    }

    // ========================================================
    // 魔法: 占有表の再構築（items が真データ、占有表は派生データ）
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
    // 魔法: そのマスの占有者
    // ========================================================
    int GetItemAt(const BackpackComponent& bp, int row, int col)
    {
        return bp.GetOccupant(row, col);
    }

    // ========================================================
    // 枠: 置けるか
    // 画布内で、他の枠と重ならなければ置ける。
    //   魔法の有無は見ない。枠は魔法の下に敷くものなので。
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
    // 枠: 置く
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

        // 置ける場所が変わったので集約側にも知らせる
        bp.dirty = true;

        return (int)bp.frames.size() - 1;
    }

    // ========================================================
    // 枠: 消す
    // 足場を失った魔法は手元へ戻す。
    //   戻す処理は ValidateItems に任せる。
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
    // 枠: 移動
    //
    // 乗っている魔法も一緒に動かす。
    // 動かした先で足場を失うもの（画布外、他の枠の魔法と重なる等）だけ
    // 手元へ戻す。全部戻すのではなく、残せるものは残す
    // （枠を少し動かすたびに全部組み直しでは編成にならない）。
    //
    // 順番が重要。枠を先に動かしてから魔法を追わせる。
    // 1. 動かす前に乗っている魔法を覚える
    // 2. 枠を動かす
    // 3. 覚えた魔法を同じだけずらす
    // 4. 足場を失ったものを戻す
    // ========================================================
    bool MoveFrame(BackpackComponent& bp, int frameIndex,
        int newRow, int newCol, int newRotation, int* outEvicted)
    {
        if (outEvicted) *outEvicted = 0;
        if (frameIndex < 0 || frameIndex >= (int)bp.frames.size()) return false;

        auto& f = bp.frames[frameIndex];

        // 移動先に置けなければ何もしない
        if (!CanPlaceFrame(bp, f.id, newRow, newCol, newRotation, frameIndex))
            return false;

        // 1. 動かす前に、この枠に乗っている魔法を覚える
        //    枠を動かした後では占有表が変わって拾えなくなる
        std::vector<int> riders = GetItemsOnFrame(bp, frameIndex);

        const int dr = newRow - f.row;
        const int dc = newCol - f.col;

        // 2. 枠を動かす
        f.row = newRow;
        f.col = newCol;
        f.rotation = ((newRotation % 4) + 4) % 4;
        RebuildFrameOccupancy(bp);

        // 3. 乗っていた魔法を同じだけずらす
        //    ※魔法は回転させない。枠の回転に魔法を追従させると
        //      置いた向きが勝手に変わって編成が壊れる
        for (int idx : riders)
        {
            if (idx < 0 || idx >= (int)bp.items.size()) continue;
            bp.items[idx].row += dr;
            bp.items[idx].col += dc;
        }
        RebuildOccupancy(bp);

        // 4. 足場を失ったものを手元へ戻す
        //    画布外に出たもの、他の魔法と重なったものがここで消える
        const int evicted = ValidateItems(bp);
        if (outEvicted) *outEvicted = evicted;

        bp.dirty = true;
        return true;
    }

    // ========================================================
    // 枠: 占有表の再構築
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
    // 整合性の検査
    //
    // 足場を失った魔法を全部手元へ戻す。
    // 「手元へ戻す」と言っても実体は消えるだけ。
    // 所持数は SpellbookComponent が持っていて、
    // 使える数 = 所持数 - グリッド上の数 を毎回数えるので、
    // グリッドから消えれば自動的に取り出せるようになる。
    //
    // 後ろから走査する理由:
    //   erase すると後ろの index が詰まる。
    //   前から走査すると詰まった分を読み飛ばしてしまう。
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

                    // 画布外
                    if (r < 0 || r >= BackpackComponent::GRID ||
                        cc < 0 || cc >= BackpackComponent::GRID)
                    {
                        ok = false;
                        break;
                    }

                    // 枠が無くなった
                    if (!bp.IsPlaceable(r, cc))
                    {
                        ok = false;
                        break;
                    }

                    // 他の魔法と重なっている
                    // 枠の移動で追従した魔法が、別の枠の魔法の上に乗ることがある。
                    // 占有表は移動後に組み直してあるので、
                    // 自分の index 以外が入っていれば重なりと判定できる。
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
    // グリッド上にある同じ ID の数
    //
    // 所持数からこれを引いたものが取り出せる数。
    // 毎回数えるので、どこで増減しても帳尻が合う。
    // 数が増えて重くなったら、その時に持ち替える。
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