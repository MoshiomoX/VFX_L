// ============================================================
// BackpackUI.cpp
// ============================================================
#include "BackpackUI.h"
#include "SpriteRenderer.h"
#include "Texture.h"
#include "BackpackComponent.h"
#include "BackpackLogic.h"
#include "ItemDatabase.h"
#include "ResourceManager.h"
#include "InputManager.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

void BackpackUI::Initialize(std::shared_ptr<Texture> blockTex)
{
    m_BlockTex = blockTex;
}

// ============================================================
// アイコン読み込み
// ※iconPath が nullptr のものは color での染色表示にフォールバックする
// ============================================================
void BackpackUI::LoadIcons()
{
    m_Icons.clear();

    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c || !c->iconPath) continue;

        auto tex = ResourceManager::Get().LoadTexture(c->iconPath);
        if (tex) m_Icons.push_back({ id, tex });
    }
}

std::shared_ptr<Texture> BackpackUI::GetIcon(ItemID id) const
{
    for (const auto& p : m_Icons)
        if (p.first == id) return p.second;
    return nullptr;
}

// ============================================================
// グリッド全体の辺の長さ
// ============================================================
float BackpackUI::GridExtent() const
{
    return m_CellSize * GRID_SIZE + m_CellGap * (GRID_SIZE - 1);
}

// ============================================================
// レイアウト計算
// ★画面短辺を基準に比率でサイズを決める。
//   縦長・横長どちらでも UI が画面外に出ない。
// ============================================================
void BackpackUI::Layout(float screenW, float screenH)
{
    m_ScreenSize = { screenW, screenH };

    float shortSide = (screenW < screenH) ? screenW : screenH;

    // グリッド全体の目標サイズから 1 マスを逆算する
    //   extent = cell * GRID + gap * (GRID - 1)、gap = cell * cellGapRatio
    //   → extent = cell * (GRID + cellGapRatio * (GRID - 1))
    float targetExtent = shortSide * gridScreenRatio;
    float denom = (float)GRID_SIZE + cellGapRatio * (float)(GRID_SIZE - 1);

    m_CellSize = targetExtent / denom;
    m_CellGap = m_CellSize * cellGapRatio;
    m_FramePad = m_CellSize * framePadRatio;

    float extent = GridExtent();
    float margin = shortSide * marginRatio;

    // ※背景板は m_Origin - m_FramePad から描くので、その分を足しておく
    switch (anchor)
    {
    case Anchor::Center:
        m_Origin.x = (screenW - extent) * 0.5f;
        m_Origin.y = (screenH - extent) * 0.5f;
        break;

    case Anchor::TopRight:
        m_Origin.x = screenW - margin - m_FramePad - extent;
        m_Origin.y = margin + m_FramePad;
        break;

    case Anchor::BottomLeft:
        m_Origin.x = margin + m_FramePad;
        m_Origin.y = screenH - margin - m_FramePad - extent;
        break;

    case Anchor::BottomRight:
        m_Origin.x = screenW - margin - m_FramePad - extent;
        m_Origin.y = screenH - margin - m_FramePad - extent;
        break;

    case Anchor::TopLeft:
    default:
        m_Origin.x = margin + m_FramePad;
        m_Origin.y = margin + m_FramePad;
        break;
    }
}

// ============================================================
// 指定マスの左上スクリーン座標
// ============================================================
Vector2 BackpackUI::CellPosition(int row, int col) const
{
    return {
        m_Origin.x + col * (m_CellSize + m_CellGap),
        m_Origin.y + row * (m_CellSize + m_CellGap)
    };
}

// ============================================================
// スクリーン座標 → グリッド座標
// ============================================================
bool BackpackUI::ScreenToCell(const Vector2& screenPos, int& outRow, int& outCol) const
{
    float pitch = m_CellSize + m_CellGap;
    if (pitch <= 0.0f) return false;

    float localX = screenPos.x - m_Origin.x;
    float localY = screenPos.y - m_Origin.y;
    if (localX < 0.0f || localY < 0.0f) return false;

    int col = (int)(localX / pitch);
    int row = (int)(localY / pitch);
    if (row < 0 || row >= GRID_SIZE || col < 0 || col >= GRID_SIZE) return false;

    // 隙間の部分をクリックした場合は無効にする
    float inCellX = localX - col * pitch;
    float inCellY = localY - row * pitch;
    if (inCellX > m_CellSize || inCellY > m_CellSize) return false;

    outRow = row;
    outCol = col;
    return true;
}

// ============================================================
// マウス操作
//   左クリック：選択中のアイテムを配置
//   右クリック：そのマスのアイテムを取り出す
//   ホイール / R：回転
// ============================================================
void BackpackUI::HandleInput(BackpackComponent& bp)
{
    auto& input = InputManager::Get();

    // ImGui がマウスを使っている時は操作しない
    if (ImGui::GetIO().WantCaptureMouse)
    {
        m_HoverRow = m_HoverCol = -1;
        m_CanPlaceHere = false;
        return;
    }

    // ★描画と同じ座標系（クライアント座標）を使う
    auto mp = input.GetMousePos();
    Vector2 mousePos = { mp.x, mp.y };

    if (!ScreenToCell(mousePos, m_HoverRow, m_HoverCol))
    {
        m_HoverRow = m_HoverCol = -1;
        m_CanPlaceHere = false;
    }
    else if (m_HasSelection)
    {
        m_CanPlaceHere = BackpackLogic::CanPlace(bp, m_SelectedItem,
            m_HoverRow, m_HoverCol, m_Rotation);
    }

    // ---- 回転（ホイール or R キー）----
    float wheel = input.GetMouseWheel();
    if (wheel > 0.0f)      m_Rotation = (m_Rotation + 1) % 4;
    else if (wheel < 0.0f) m_Rotation = (m_Rotation + 3) % 4;
    if (input.GetKeyTrigger('R')) m_Rotation = (m_Rotation + 1) % 4;

    if (m_HoverRow < 0) return;

    // ---- 左クリック：配置 ----
    if (input.GetMouseTrigger(0) && m_HasSelection)
        BackpackLogic::Place(bp, m_SelectedItem, m_HoverRow, m_HoverCol, m_Rotation);

    // ---- 右クリック：取り出す ----
    if (input.GetMouseTrigger(1))
    {
        int idx = BackpackLogic::GetItemAt(bp, m_HoverRow, m_HoverCol);
        if (idx >= 0)
            BackpackLogic::Remove(bp, idx);
    }
}

// ============================================================
// 描画
// ※描画順 = 重なり順
//   背景板 → 空きマス → 影響格 → アイテム本体 → 配置プレビュー
// ============================================================
void BackpackUI::Draw(SpriteRenderer& sprite, const BackpackComponent& bp)
{
    if (!m_BlockTex) return;

    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };

    // ---- 背景板 ----
    float extent = GridExtent();
    Vector2 framePos = { m_Origin.x - m_FramePad, m_Origin.y - m_FramePad };
    Vector2 frameSize = { extent + m_FramePad * 2.0f, extent + m_FramePad * 2.0f };
    sprite.Draw(m_BlockTex, framePos, frameSize, frameColor);

    // ---- 空きマス（全マスに底として描く）----
    for (int row = 0; row < GRID_SIZE; ++row)
        for (int col = 0; col < GRID_SIZE; ++col)
            sprite.Draw(m_BlockTex, CellPosition(row, col), cellSizeVec, cellColor);

    // ---- 影響格（アイテム色の薄い版。本体より先に描いて下に敷く）----
    for (const auto& item : bp.items)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(item.id);
        if (!c || c->influenceCells.empty()) continue;

        Vector4 col = c->color;
        col.w = 0.28f;

        auto cells = BackpackLogic::RotateShape(c->influenceCells, item.rotation);
        for (const auto& off : cells)
        {
            int r = item.row + off.row;
            int cc = item.col + off.col;
            if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

            sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, col);
        }
    }

    // ---- アイテム本体（占位格）----
    // アイコンがあれば白で描く（画像そのままの色）、無ければ色染め
    for (const auto& item : bp.items)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(item.id);
        if (!c) continue;

        auto icon = GetIcon(item.id);
        auto tex = icon ? icon : m_BlockTex;
        Vector4 col = icon ? Vector4(1, 1, 1, 1) : c->color;

        auto cells = BackpackLogic::RotateShape(c->occupyCells, item.rotation);
        for (const auto& off : cells)
        {
            sprite.Draw(tex, CellPosition(item.row + off.row, item.col + off.col),
                cellSizeVec, col);
        }
    }

    // ---- 配置プレビュー ----
    if (m_HasSelection && m_HoverRow >= 0)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(m_SelectedItem);
        if (c)
        {
            // 影響格のプレビュー（薄く、本体より先に）
            auto infl = BackpackLogic::RotateShape(c->influenceCells, m_Rotation);
            for (const auto& off : infl)
            {
                int r = m_HoverRow + off.row;
                int cc = m_HoverCol + off.col;
                if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

                Vector4 ic = c->color;
                ic.w = 0.20f;
                sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, ic);
            }

            // 占位格のプレビュー（置ける=緑、置けない=赤）
            Vector4 col = m_CanPlaceHere ? Vector4(0.4f, 1.0f, 0.5f, 0.55f)
                : Vector4(1.0f, 0.3f, 0.3f, 0.55f);

            auto cells = BackpackLogic::RotateShape(c->occupyCells, m_Rotation);
            for (const auto& off : cells)
            {
                int r = m_HoverRow + off.row;
                int cc = m_HoverCol + off.col;
                if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

                sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, col);
            }
        }
    }
}