// ============================================================
// BackpackUI.h
// グリッドの描画と操作。
//
// レイアウトは画面短辺に対する比率で持つ。
//   ピクセル直指定にすると解像度が変わった時に破綻するため。
// 当たり判定と描画は同じ座標系（クライアント座標ピクセル）を使う。
//   マウス位置は InputManager から取る（ImGui の viewport 設定に依存しない）。
//
// 2つの編集モードを持つ：
//   Spell … 魔法ブロックを置く。枠の上にしか置けない。
//   Frame … 設置枠を置く。枠同士は重ならない。
//
// 操作はドラッグ&ドロップ：
//   掴む   … 押した瞬間の「マウス - ブロック原点」を保持する
//   運ぶ   … ピクセル単位で自由に動く（マス目に吸い付かない）
//   落とす … ずれ量を最寄りのマスへ丸めて確定する
// ============================================================
#pragma once
#include "SpellID.h"
#include "BackpackComponent.h"   // static_assert で GRID を参照するため必須
#include <memory>
#include <vector>
#include <SimpleMath.h>

class SpriteRenderer;
class Texture;
struct SpellbookComponent;
class BackpackUI
{
public:
    static constexpr int GRID_SIZE = 7;
    static_assert(GRID_SIZE == BackpackComponent::GRID,
        "UI とロジックのグリッド寸法が食い違っている");

    // ---- 編集モード ----
    enum class EditMode { Spell, Frame };

    void Initialize(std::shared_ptr<Texture> blockTex);
    void LoadIcons();                        // ItemDatabase の iconPath を辿って読み込む
    void Layout(float screenW, float screenH);

    void HandleInput(BackpackComponent& bp);
    void Draw(SpriteRenderer& sprite, const BackpackComponent& bp);

    bool ScreenToCell(const DirectX::SimpleMath::Vector2& screenPos,
        int& outRow, int& outCol) const;

    // ---- パレット（ImGui から設定する）----
    // ここで選ぶと「次にグリッドで押した時に、その種類を新規で掴む」状態になる。
    void SetSelectedItem(ItemID id) { m_SelectedItem = id; m_HasSelection = true; }
    void ClearSelection() { m_HasSelection = false; }
    bool HasSelection() const { return m_HasSelection; }
    ItemID GetSelectedItem() const { return m_SelectedItem; }
    int  GetRotation() const { return m_Rotation; }

    // ---- モード ----
    void     SetEditMode(EditMode m) { m_EditMode = m; ClearSelection(); CancelDrag(); }
    EditMode GetEditMode() const { return m_EditMode; }

    // ---- 直近の操作結果（ImGui 表示用）----
    // 枠を動かした結果、足場を失って外された魔法の数
    int  GetLastEvicted() const { return m_LastEvicted; }
    void ClearLastEvicted() { m_LastEvicted = 0; }
    bool IsDragging() const { return m_Drag.source != DragSource::None; }

    // ---- 配置基準 ----
    enum class Anchor { TopLeft, TopRight, Center, BottomLeft, BottomRight };
    Anchor anchor = Anchor::TopLeft;

    // ---- レイアウト（すべて比率。実ピクセルは Layout で算出）----
    float gridScreenRatio = 0.42f;   // グリッド全体が画面短辺の何割か
    float cellGapRatio = 0.07f;   // 1マスに対する隙間の比率
    float framePadRatio = 0.28f;   // 1マスに対する背景板余白の比率
    float marginRatio = 0.02f;   // 画面端からの余白（短辺基準）

    DirectX::SimpleMath::Vector4 frameColor = { 0.22f, 0.15f, 0.10f, 0.92f };
    DirectX::SimpleMath::Vector4 cellColor = { 0.45f, 0.32f, 0.22f, 1.00f };

    // 枠が敷かれていないマス。暗く沈めて「ここは使えない」を示す。
    DirectX::SimpleMath::Vector4 lockedCellColor = { 0.14f, 0.11f, 0.09f, 0.85f };

    // ---- 表示の切り替え ----
    // 影響格を常時表示すると画面が色だらけになり、
    // どのブロックがどこへ影響しているのか却って分からなくなる。
    // 触れている1つだけを見せる。
    bool showInfluenceOnHover = true;
    bool highlightInfluenced = true;

    // 運んでいるブロックの不透明度（下のグリッドが透けて見えるように）
    float dragAlpha = 0.75f;


    // ---- 算出結果の参照（ImGui 表示用）----
    float GetCellSize() const { return m_CellSize; }
    float GetCellGap()  const { return m_CellGap; }
    void SetSpellbook(const SpellbookComponent* book) { m_Spellbook = book; }

    // パレットから新規に取り出せるか（所持数 - 配置数 > 0）
    int GetAvailableCount(const BackpackComponent& bp, ItemID id) const;
private:
    // ============================================================
    // ドラッグ状態
    //
    // ドラッグ中はグリッドのデータを一切書き換えない。
    // 掴んだブロックはグリッド上に残したままにして、
    // 描画で飛ばし、判定では ignoreIndex で無視する。
    // 途中で中断しても元の状態が壊れないようにするため。
    // ============================================================
    enum class DragSource
    {
        None,
        Palette,   // パレットから新規に持ってきた
        Grid,      // グリッド上の既存ブロックを掴んだ
    };

    struct DragState
    {
        DragSource source = DragSource::None;
        ItemID     id = ItemID::Fireball;
        int        rotation = 0;

        // グリッドから掴んだ場合の元データ（置けなかった時は動かさない）
        int originalIndex = -1;

        // 掴んだ瞬間の「マウス位置 - ブロック原点のスクリーン座標」。
        // これを保ち続けることで、掴んだ場所を指したまま運べる。
        DirectX::SimpleMath::Vector2 grabOffset = { 0.0f, 0.0f };

        // 落とし先（マス目へ丸めた結果）。毎フレーム更新する。
        int  dropRow = 0;
        int  dropCol = 0;
        bool canDrop = false;
    };

    void BeginDrag(BackpackComponent& bp, const DirectX::SimpleMath::Vector2& mousePos);
    void UpdateDrag(BackpackComponent& bp, const DirectX::SimpleMath::Vector2& mousePos);
    void EndDrag(BackpackComponent& bp);
    void CancelDrag() { m_Drag = DragState{}; }

    DirectX::SimpleMath::Vector2 CellPosition(int row, int col) const;
    float GridExtent() const;
    float CellPitch() const { return m_CellSize + m_CellGap; }
    std::shared_ptr<Texture> GetIcon(ItemID id) const;

    void DrawHoverInfluence(SpriteRenderer& sprite, const BackpackComponent& bp);
    void DrawDragged(SpriteRenderer& sprite, const DirectX::SimpleMath::Vector2& mousePos);
    void DrawDropShadow(SpriteRenderer& sprite);

    std::shared_ptr<Texture> m_BlockTex;
    std::vector<std::pair<ItemID, std::shared_ptr<Texture>>> m_Icons;

    DirectX::SimpleMath::Vector2 m_Origin = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_ScreenSize = { 1600.0f, 900.0f };
    DirectX::SimpleMath::Vector2 m_MousePos = { 0.0f, 0.0f };

    // Layout で算出される実ピクセル値
    float m_CellSize = 56.0f;
    float m_CellGap = 4.0f;
    float m_FramePad = 16.0f;

    // パレットで選んでいるもの
    ItemID   m_SelectedItem = ItemID::Fireball;
    bool     m_HasSelection = false;
    int      m_Rotation = 0;
    EditMode m_EditMode = EditMode::Spell;
    const  SpellbookComponent* m_Spellbook = nullptr;

    DragState m_Drag;

    // マウスが乗っているマス（-1 = グリッド外）
    int  m_HoverRow = -1;
    int  m_HoverCol = -1;

    // 乗っているブロック / 枠のインデックス（-1 = 無し）
    int  m_HoverItemIndex = -1;
    int  m_HoverFrameIndex = -1;

    int  m_LastEvicted = 0;
};