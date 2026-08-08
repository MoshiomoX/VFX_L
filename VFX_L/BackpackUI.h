// ============================================================
// BackpackUI.h
// バックパック（7x7 グリッド）の描画と操作。
//
// ★レイアウトは画面短辺に対する比率で持つ。
//   ピクセル直指定にすると解像度が変わった時に破綻するため。
// ★当たり判定と描画は同じ座標系（クライアント座標ピクセル）を使う。
//   マウス位置は InputManager から取る（ImGui の viewport 設定に依存しない）。
// ============================================================
#pragma once
#include "SpellID.h"
#include <memory>
#include <vector>
#include <SimpleMath.h>

class SpriteRenderer;
class Texture;
struct BackpackComponent;

class BackpackUI
{
public:
    static constexpr int GRID_SIZE = 7;

    void Initialize(std::shared_ptr<Texture> blockTex);
    void LoadIcons();                        // ItemDatabase の iconPath を辿って読み込む
    void Layout(float screenW, float screenH);

    void HandleInput(BackpackComponent& bp);
    void Draw(SpriteRenderer& sprite, const BackpackComponent& bp);

    bool ScreenToCell(const DirectX::SimpleMath::Vector2& screenPos,
        int& outRow, int& outCol) const;

    // ---- 選択中のアイテム（パレット。ImGui から設定する）----
    void SetSelectedItem(ItemID id) { m_SelectedItem = id; m_HasSelection = true; }
    void ClearSelection() { m_HasSelection = false; }
    bool HasSelection() const { return m_HasSelection; }
    ItemID GetSelectedItem() const { return m_SelectedItem; }
    int  GetRotation() const { return m_Rotation; }

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

    // ---- 算出結果の参照（ImGui 表示用）----
    float GetCellSize() const { return m_CellSize; }
    float GetCellGap()  const { return m_CellGap; }

private:
    DirectX::SimpleMath::Vector2 CellPosition(int row, int col) const;
    float GridExtent() const;
    std::shared_ptr<Texture> GetIcon(ItemID id) const;

    std::shared_ptr<Texture> m_BlockTex;
    std::vector<std::pair<ItemID, std::shared_ptr<Texture>>> m_Icons;

    DirectX::SimpleMath::Vector2 m_Origin = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_ScreenSize = { 1600.0f, 900.0f };

    // Layout で算出される実ピクセル値
    float m_CellSize = 56.0f;
    float m_CellGap = 4.0f;
    float m_FramePad = 16.0f;

    // 選択中のアイテム
    ItemID m_SelectedItem = ItemID::Fireball;
    bool   m_HasSelection = false;
    int    m_Rotation = 0;

    // マウスが乗っているマス（-1 = グリッド外）
    int  m_HoverRow = -1;
    int  m_HoverCol = -1;
    bool m_CanPlaceHere = false;
};