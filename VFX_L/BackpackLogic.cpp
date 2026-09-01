// ============================================================
// BackpackLogic.cpp
// ============================================================
#include "BackpackLogic.h"
#include "ItemDatabase.h"

namespace BackpackLogic
{
    // ========================================================
// その枠に乗っている魔法の一覧
//
// 占位格が1マスでもその枠に重なっていれば「乗っている」と数える。
// 複数の枠にまたがる魔法は、どちらの枠を動かしても付いてくる。
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
    // 魔法：配置可能判定
    // ※条件が2つある。枠の上であること、他の魔法と重ならないこと。
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

            // ※枠が敷かれていないマスには置けない
            if (!bp.IsPlaceable(r, cc))
                return false;

            // 他のアイテムの占位格と重なる
            int occ = bp.GetOccupant(r, cc);
            if (occ >= 0 && occ != ignoreIndex)
                return false;
        }
        return true;
    }

    // ========================================================
    // 魔法：配置
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
    // 魔法：削除
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
    // 魔法：占有表の再構築（items が真データ、占有表は派生データ）
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

    // ========================================================
    // 枠：配置可能判定
    // ※魔法との重なりは見ない。枠の上に魔法が乗るのが正常。
    //   見るのは「グリッド内か」と「他の枠と重ならないか」だけ。
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
    // 枠：配置
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

        // 枠が増えるだけなので、既存の魔法が無効になることはない
        bp.dirty = true;

        return (int)bp.frames.size() - 1;
    }

    // ========================================================
    // 枠：削除
    // ※上に魔法が乗っていても外せる。
    //   足場を失った魔法は ValidateItems が取り除く。
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
 // 枠：移動
 //
 // 枠の上に乗っている魔法も一緒に運ぶ。
 // 枠は魔法を並べる台なので、台を動かしたら上の物も付いてくる方が自然。
 // これが無いと、配置を組んだ後に枠を動かせなくなる
 // （動かした瞬間に全部手元へ戻ってしまうため）。
 //
 // 移動先で置けなくなった魔法だけが手元へ戻る。
 // 1つでも駄目なら枠ごと動かさない、という作りにはしない。
 // 「枠は入るのに動かせない」という状態の方が分かりにくいため。
 // ========================================================
    bool MoveFrame(BackpackComponent& bp, int frameIndex,
        int newRow, int newCol, int newRotation, int* outEvicted)
    {
        if (outEvicted) *outEvicted = 0;
        if (frameIndex < 0 || frameIndex >= (int)bp.frames.size()) return false;

        auto& f = bp.frames[frameIndex];

        // 自分自身の占有は無視して判定する
        if (!CanPlaceFrame(bp, f.id, newRow, newCol, newRotation, frameIndex))
            return false;

        // 1. 動かす前に「この枠に乗っている魔法」を控えておく
        //    回転すると枠の形が変わるので、必ず移動前の状態で調べる。
        std::vector<int> riders = GetItemsOnFrame(bp, frameIndex);

        const int dr = newRow - f.row;
        const int dc = newCol - f.col;

        // 2. 枠を動かす
        f.row = newRow;
        f.col = newCol;
        f.rotation = ((newRotation % 4) + 4) % 4;
        RebuildFrameOccupancy(bp);

        // 3. 乗っていた魔法を同じだけ平行移動する
        //    回転は追従させない。ブロックの向きは影響格の向きでもあるので、
        //    枠を回した拍子に効果の向きまで変わると事故になる。
        for (int idx : riders)
        {
            if (idx < 0 || idx >= (int)bp.items.size()) continue;
            bp.items[idx].row += dr;
            bp.items[idx].col += dc;
        }
        RebuildOccupancy(bp);

        // 4. 移動先で成立しなくなったものを取り除く
        //    枠から外れた場合と、他のブロックと重なった場合の両方を見る。
        const int evicted = ValidateItems(bp);
        if (outEvicted) *outEvicted = evicted;

        bp.dirty = true;
        return true;
    }
    // ========================================================
    // 枠：占有表の再構築
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
  // 整合性の回復
  //
  // 取り除くだけで、どこかへ移す処理は要らない。
  // 所持数は SpellbookComponent が持ち、
  // 「使える数 = 所持数 - グリッド上の数」で求めるため、
  // グリッドから消えた時点で自動的に手元に戻っている。
  //
  // 後ろから走査する理由：
  //   erase するとそれ以降のインデックスがずれる。
  //   後ろから消せば、まだ見ていない前側のインデックスは動かない。
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

                    // 範囲外
                    if (r < 0 || r >= BackpackComponent::GRID ||
                        cc < 0 || cc >= BackpackComponent::GRID)
                    {
                        ok = false;
                        break;
                    }

                    // 枠から外れている
                    if (!bp.IsPlaceable(r, cc))
                    {
                        ok = false;
                        break;
                    }

                    // 他のブロックと重なっている
                    // 枠と一緒に平行移動した結果、別のブロックと衝突する場合がある。
                    // 占有表は自分自身を指しているはずなので、
                    // 別の index が入っていたら重なりが起きている。
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
// グリッド上に置いてある同じ ID の数
//
// 「あと何個置けるか」は所持数からこれを引いて求める。
// 使用中の数を別に持たないことで、二重管理を避けている。
// マス数は多くないので毎回数えても問題にならない。
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