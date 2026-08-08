// ============================================================
// BackpackLogic.h
// グリッドへの配置・削除・形状回転の判定ロジック（純関数群）。
// ECS のコンポーネントを直接操作するが、System ではなく道具として使う。
// ============================================================
#pragma once
#include "BackpackComponent.h"
#include "ItemTypes.h"
#include <vector>

namespace BackpackLogic
{
    // 形状を rotation（0~3）ぶん回転させた相対座標を返す
    // ★回転は (row, col) → (col, -row) を rotation 回。
    //   占位格と影響格の両方に同じ変換を適用するので、
    //   回転すると「置けるか」と「どこに影響するか」が同時に変わる。
    std::vector<CellOffset> RotateShape(const std::vector<CellOffset>& shape, int rotation);

    // 指定位置に置けるか（範囲内 + 他の占位格と重ならない）
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
}