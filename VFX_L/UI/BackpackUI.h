// ============================================================
// BackpackUI.h
// グリッド編成の UI。
//
// 描画は SpriteRenderer に相乗りする。
// 入力は自分で読む（モーダル UI として UIManager が排他している前提）。
// ドラッグ状態は持たない。GameUI の DragContext を借りて読み書きする。
//
// 編集モードは無い。掴んだ物が枠か魔法かは ItemDatabase::IsFrame(id) が
// 決める。プレイヤーに「今どのモードか」を覚えさせない。
//   掴む優先順位: 魔法（上に乗っている）→ 枠（下に敷かれている）
// ============================================================
#pragma once
#include "SpellID.h"
#include "Component/BackpackComponent.h"   // static_assert で GRID を縛るために必要
#include "UI/DragContext.h"
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
        "BackpackUI::GRID_SIZE must match BackpackComponent::GRID");

    void Initialize(std::shared_ptr<Texture> blockTex);
    void LoadIcons();                        // ItemDatabase の iconPath を読み込む
    void Layout(float screenW, float screenH);

    // GameUI が Initialize の直後に一度だけ呼ぶ。以後 null にはならない
    void SetDragContext(DragContext* drag) { m_Drag = drag; }

    void HandleInput(BackpackComponent& bp);
    void Draw(SpriteRenderer& sprite, const BackpackComponent& bp);

    bool ScreenToCell(const DirectX::SimpleMath::Vector2& screenPos,
        int& outRow, int& outCol) const;

    // ---- 選択中（ImGui のパレットから設定する）----
    void SetSelectedItem(ItemID id) { m_SelectedItem = id; m_HasSelection = true; }
    void ClearSelection() { m_HasSelection = false; }
    bool HasSelection() const { return m_HasSelection; }
    ItemID GetSelectedItem() const { return m_SelectedItem; }
    int  GetRotation() const { return m_Rotation; }

    // ---- 通知（ImGui 表示用）----
    int  GetLastEvicted() const { return m_LastEvicted; }
    void ClearLastEvicted() { m_LastEvicted = 0; }
    bool IsDragging() const { return m_Drag && m_Drag->IsActive(); }

    // ---- 配置位置 ----
    enum class Anchor { TopLeft, TopRight, Center, BottomLeft, BottomRight };
    Anchor anchor = Anchor::TopLeft;

    // ---- 見た目（すべて画面短辺に対する比率。Layout で確定）----
    float gridScreenRatio = 0.42f;
    float cellGapRatio = 0.07f;
    float framePadRatio = 0.28f;
    float marginRatio = 0.02f;

    DirectX::SimpleMath::Vector4 frameColor = { 0.22f, 0.15f, 0.10f, 0.92f };
    DirectX::SimpleMath::Vector4 cellColor = { 0.45f, 0.32f, 0.22f, 1.00f };
    DirectX::SimpleMath::Vector4 lockedCellColor = { 0.14f, 0.11f, 0.09f, 0.85f };

    // ---- 影響格の表示 ----
    bool showInfluenceOnHover = true;
    bool highlightInfluenced = true;
    float dragAlpha = 0.75f;

    // ---- 読み取り用 ----
    float GetCellSize() const { return m_CellSize; }
    float GetCellGap()  const { return m_CellGap; }
    void SetSpellbook(const SpellbookComponent* book) { m_Spellbook = book; }
    int GetAvailableCount(const BackpackComponent& bp, ItemID id) const;

private:
    void BeginDrag(BackpackComponent& bp, const DirectX::SimpleMath::Vector2& mousePos);
    void UpdateDrag(BackpackComponent& bp, const DirectX::SimpleMath::Vector2& mousePos);
    void EndDrag(BackpackComponent& bp);
    void CancelDrag() { if (m_Drag) m_Drag->Reset(); }

    DirectX::SimpleMath::Vector2 CellPosition(int row, int col) const;
    float GridExtent() const;
    float CellPitch() const { return m_CellSize + m_CellGap; }
    std::shared_ptr<Texture> GetIcon(ItemID id) const;

    void DrawHoverInfluence(SpriteRenderer& sprite, const BackpackComponent& bp);
    void DrawDragged(SpriteRenderer& sprite, const DirectX::SimpleMath::Vector2& mousePos);
    void DrawDropShadow(SpriteRenderer& sprite);

    std::shared_ptr<Texture> m_BlockTex;
    std::vector<std::pair<ItemID, std::shared_ptr<Texture>>> m_Icons;

    DragContext* m_Drag = nullptr;

    DirectX::SimpleMath::Vector2 m_Origin = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_ScreenSize = { 1600.0f, 900.0f };
    DirectX::SimpleMath::Vector2 m_MousePos = { 0.0f, 0.0f };

    float m_CellSize = 56.0f;
    float m_CellGap = 4.0f;
    float m_FramePad = 16.0f;

    ItemID   m_SelectedItem = ItemID::Fireball;
    bool     m_HasSelection = false;
    int      m_Rotation = 0;
    const  SpellbookComponent* m_Spellbook = nullptr;

    int  m_HoverRow = -1;
    int  m_HoverCol = -1;
    int  m_HoverItemIndex = -1;
    int  m_HoverFrameIndex = -1;

    int  m_LastEvicted = 0;
};