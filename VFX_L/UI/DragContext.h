// ============================================================
// DragContext.h
// UI をまたぐドラッグの状態。GameUI が1つだけ持つ。
//
// BackpackUI と SpellbookUI が同じ実体を見る。
//   「魔法書から掴んでグリッドに落とす」を成立させるには、
//   掴んだ側と落とす側が同じ状態を読めないといけない。
//
// ドラッグ中はデータ（BackpackComponent / SpellbookComponent）を
// 書き換えない。確定は落とした瞬間だけ。
// ============================================================
#pragma once
#include "SpellID.h"
#include <SimpleMath.h>

// どこから掴んだか
enum class DragSource
{
    None,
    Palette,        // ImGui のパレットから（デバッグ用の旧経路）
    BackpackGrid,   // グリッド上にある既存のものを掴んだ
    Spellbook,      // 魔法書の箱から掴んだ
};

struct DragContext
{
    DragSource source = DragSource::None;
    ItemID     id = ItemID::Fireball;
    int        rotation = 0;

    // グリッドから掴んだ時の元の index（判定で自分を除外する用）
    // BackpackGrid 以外では -1
    int originalIndex = -1;

    // 掴んだ位置とアンカーのずれ - アンカー左上からマウスまでの距離。
    // これを引いてからマスに丸めると、掴んだ場所がずれずに付いてくる
    DirectX::SimpleMath::Vector2 grabOffset = { 0.0f, 0.0f };

    // 置き先（マスに丸めた後）と、そこに置けるかどうか
    int  dropRow = 0;
    int  dropCol = 0;
    bool canDrop = false;

    // ---- 拾い上げの見た目（判定には一切使わない）----
    // 箱から掴んだ瞬間に body の角度と縮小率が入り、
    // SpellbookUI::Update が毎フレーム 0 / 1 へ寄せていく。
    // BackpackUI::DrawDragged が描画に反映する。
    float visAngle = 0.0f;   // rad。0 へ収束
    float visScale = 1.0f;   // 1 へ収束

    bool IsActive() const { return source != DragSource::None; }
    void Reset() { *this = DragContext{}; }
};