// ============================================================
// BackpackLogic.h
// グリッドへの配置・削除・形状回転の判定ロジック（純関数群）。
// ECS のコンポーネントを直接操作するが、System ではなく道具として使う。
//
// 魔法と枠で関数を分けている。判定条件が違うため：
//     魔法 … 枠の上、かつ他の魔法と重ならない
//     枠   … グリッド内、かつ他の枠と重ならない（魔法とは重なってよい）
// ============================================================
#pragma once
#include "BackpackComponent.h"
#include "ItemTypes.h"
#include <vector>

namespace BackpackLogic
{
    // 形状を rotation（0~3）ぶん回転させた相対座標を返す
    //   回転は (row, col) → (col, -row) を rotation 回。
    //   占位格と影響格の両方に同じ変換を適用するので、
    //   回転すると「置けるか」と「どこに影響するか」が同時に変わる。
    std::vector<CellOffset> RotateShape(const std::vector<CellOffset>& shape, int rotation);

    // ============================================================
    // 魔法ブロック
    // ============================================================

    // 指定位置に置けるか（枠の上 + 他の魔法と重ならない）
    // ignoreIndex を指定すると、そのアイテムの占有を無視して判定する（移動時に使う）
    bool CanPlace(const BackpackComponent& bp, ItemID id,
        int row, int col, int rotation, int ignoreIndex = -1);

    // 配置する。成功したら items のインデックスを返す。失敗は -1
    int Place(BackpackComponent& bp, ItemID id, int row, int col, int rotation);

    // 指定インデックスのアイテムを削除
    void Remove(BackpackComponent& bp, int itemIndex);

    // 占有表を items から作り直す
    void RebuildOccupancy(BackpackComponent& bp);

    // そのマスを占めているアイテムのインデックス（-1 = 空）
    int GetItemAt(const BackpackComponent& bp, int row, int col);

    // ============================================================
    // 設置枠
    // ============================================================

    // 枠を置けるか（グリッド内 + 他の枠と重ならない）
    //   魔法との重なりは見ない。枠の上に魔法が乗るのが正常な状態。
    bool CanPlaceFrame(const BackpackComponent& bp, ItemID id,
        int row, int col, int rotation, int ignoreIndex = -1);

    // 枠を置く。成功したら frames のインデックスを返す。失敗は -1
    // 置いた後に ValidateItems を呼ぶ必要はない（枠が増えるだけなので）
    int PlaceFrame(BackpackComponent& bp, ItemID id, int row, int col, int rotation);

    // 枠を外す。
    // 戻り値は「枠が減ったことで置けなくなり、外された魔法の数」。
    //   枠の上に魔法が残っていても外せる。魔法は自動で戻る。
    int RemoveFrame(BackpackComponent& bp, int frameIndex);

    // 枠を移動する。成功したら true。
    // ※戻り値の outEvicted に、置けなくなって外された魔法の数が入る。
    bool MoveFrame(BackpackComponent& bp, int frameIndex,
        int newRow, int newCol, int newRotation, int* outEvicted = nullptr);

    // 枠の占有表を frames から作り直す
    void RebuildFrameOccupancy(BackpackComponent& bp);

    // そのマスの枠のインデックス（-1 = 枠なし）
    int GetFrameAt(const BackpackComponent& bp, int row, int col);

    // ============================================================
    // 整合性の回復
    // ============================================================

    // 枠から外れてしまった魔法を取り除く。
    // 取り除くだけで、どこかへ移す処理は要らない。
    //   所持数は SpellbookComponent が持ち、
    //   「使える数 = 所持数 - グリッド上の数」で求めるため、
    //   グリッドから消えた時点で自動的に手元に戻っている。
    // 戻り値は取り除いた個数。
    int ValidateItems(BackpackComponent& bp);
    int CountPlaced(const BackpackComponent& bp, ItemID id);
    int CountPlacedFrames(const BackpackComponent& bp, ItemID id);
    std::vector<int> GetItemsOnFrame(const BackpackComponent& bp, int frameIndex);
}