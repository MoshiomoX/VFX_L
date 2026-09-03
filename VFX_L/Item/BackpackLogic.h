// ============================================================
// BackpackLogic.h
// バックパックの配置ロジック（純関数の集まり）。
// ECS の Component は触るが System にはしない（毎フレーム走らない）。
//
// 魔法と枠で関数を分けている理由:
//     魔法 . 枠の上にしか置けない（frameOccupancy を見る）
//     枠   . 画布の中ならどこでも置ける（重なり判定は枠同士だけ）
// ============================================================
#pragma once
#include "Component/BackpackComponent.h"
#include "Item/ItemTypes.h"
#include <vector>

namespace BackpackLogic
{
    // 形状を rotation（0~3）ぶん時計回りに回す
    //   1回で (row, col) → (col, -row) に写す
    //   アンカーは動かさない（回転の中心はアンカー）
    //   返り値はそのまま占有表の書き込みに使える
    std::vector<CellOffset> RotateShape(const std::vector<CellOffset>& shape, int rotation);

    // ============================================================
    // 魔法ブロック
    // ============================================================

    // 置けるか（画布内 + 枠の上 + 空いている）
    // ignoreIndex を渡すと、そのアイテムは無いものとして判定する（移動用）
    bool CanPlace(const BackpackComponent& bp, ItemID id,
        int row, int col, int rotation, int ignoreIndex = -1);

    // 置く。成功なら items 内の index、失敗なら -1
    int Place(BackpackComponent& bp, ItemID id, int row, int col, int rotation);

    // 消す（占有表も更新する）
    void Remove(BackpackComponent& bp, int itemIndex);

    // 占有表を items から組み直す
    void RebuildOccupancy(BackpackComponent& bp);

    // そのマスに乗っているアイテムの index（-1 = 空）
    int GetItemAt(const BackpackComponent& bp, int row, int col);

    // ============================================================
    // 設置枠
    // ============================================================

    // 枠を置けるか（画布内 + 他の枠と重ならない）
    //   魔法の有無は見ない。枠は魔法の下に敷くものなので。
    bool CanPlaceFrame(const BackpackComponent& bp, ItemID id,
        int row, int col, int rotation, int ignoreIndex = -1);

    // 枠を置く。成功なら frames 内の index、失敗なら -1
    // ※置くだけなら ValidateItems は要らない（置ける場所が増えるだけ）
    int PlaceFrame(BackpackComponent& bp, ItemID id, int row, int col, int rotation);

    // 枠を消す。
    // 足場を失った魔法は手元へ戻す。戻した数を返す。
    //   ※乗っていた魔法の扱いを呼ぶ側に任せない
    int RemoveFrame(BackpackComponent& bp, int frameIndex);

    // 枠を動かす。動かせたら true。
    // 足場を失って戻された魔法の数を outEvicted で返す
    bool MoveFrame(BackpackComponent& bp, int frameIndex,
        int newRow, int newCol, int newRotation, int* outEvicted = nullptr);

    // 枠の占有表を frames から組み直す
    void RebuildFrameOccupancy(BackpackComponent& bp);

    // そのマスに敷かれている枠の index（-1 = 枠なし）
    int GetFrameAt(const BackpackComponent& bp, int row, int col);

    // ============================================================
    // 整合性
    // ============================================================

    // 足場を失った魔法を全部手元へ戻す。戻した数を返す。
    // 「手元へ戻す」と言っても実体は消えるだけ。
    //   所持数は SpellbookComponent が持っていて、
    //   使える数 = 所持数 - グリッド上の数 を毎回数えるので、
    //   グリッドから消えれば自動的に取り出せるようになる。
    // 枠を減らす操作の後に必ず呼ぶ。
    int ValidateItems(BackpackComponent& bp);
    int CountPlaced(const BackpackComponent& bp, ItemID id);
    int CountPlacedFrames(const BackpackComponent& bp, ItemID id);
    std::vector<int> GetItemsOnFrame(const BackpackComponent& bp, int frameIndex);
}