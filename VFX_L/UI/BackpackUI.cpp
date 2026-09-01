// ============================================================
// BackpackUI.cpp
// ============================================================
#include "UI/BackpackUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Component/SpellbookComponent.h"
#include "Graphics/Material/Texture.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "Manager/ResourceManager.h"
#include "Manager/InputManager.h"
#include "imgui.h"
#include <cmath>

using namespace DirectX::SimpleMath;

void BackpackUI::Initialize(std::shared_ptr<Texture> blockTex)
{
    m_BlockTex = blockTex;
}

// ============================================================
// ????????
// iconPath ? nullptr ???? color ????????????????
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
// ???????????
// ============================================================
float BackpackUI::GridExtent() const
{
    return m_CellSize * GRID_SIZE + m_CellGap * (GRID_SIZE - 1);
}

// ============================================================
// ???????
// ???????????????????
//   ?????????? UI ?????????
// ============================================================
void BackpackUI::Layout(float screenW, float screenH)
{
    m_ScreenSize = { screenW, screenH };

    float shortSide = (screenW < screenH) ? screenW : screenH;

    // ?????????????? 1 ???????
    //   extent = cell * GRID + gap * (GRID - 1)?gap = cell * cellGapRatio
    //   ? extent = cell * (GRID + cellGapRatio * (GRID - 1))
    float targetExtent = shortSide * gridScreenRatio;
    float denom = (float)GRID_SIZE + cellGapRatio * (float)(GRID_SIZE - 1);

    m_CellSize = targetExtent / denom;
    m_CellGap = m_CellSize * cellGapRatio;
    m_FramePad = m_CellSize * framePadRatio;

    float extent = GridExtent();
    float margin = shortSide * marginRatio;

    // ???? m_Origin - m_FramePad ????????????????
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
// ??????????????
// ============================================================
Vector2 BackpackUI::CellPosition(int row, int col) const
{
    return {
        m_Origin.x + col * CellPitch(),
        m_Origin.y + row * CellPitch()
    };
}

// ============================================================
// ??????? ? ??????
// ============================================================
bool BackpackUI::ScreenToCell(const Vector2& screenPos, int& outRow, int& outCol) const
{
    float pitch = CellPitch();
    if (pitch <= 0.0f) return false;

    float localX = screenPos.x - m_Origin.x;
    float localY = screenPos.y - m_Origin.y;
    if (localX < 0.0f || localY < 0.0f) return false;

    int col = (int)(localX / pitch);
    int row = (int)(localY / pitch);
    if (row < 0 || row >= GRID_SIZE || col < 0 || col >= GRID_SIZE) return false;

    // ????????????????????
    float inCellX = localX - col * pitch;
    float inCellY = localY - row * pitch;
    if (inCellX > m_CellSize || inCellY > m_CellSize) return false;

    outRow = row;
    outCol = col;
    return true;
}

// ============================================================
// ??
//
// ??????????????????????????
// ????????????????????????????
// ============================================================
void BackpackUI::BeginDrag(BackpackComponent& bp, const Vector2& mousePos)
{
    const bool frameMode = (m_EditMode == EditMode::Frame);

    int row = 0, col = 0;
    if (!ScreenToCell(mousePos, row, col)) return;

    const int existing = frameMode
        ? BackpackLogic::GetFrameAt(bp, row, col)
        : BackpackLogic::GetItemAt(bp, row, col);

    if (existing >= 0)
    {
        // ---- ???????????? ----
        const auto& src = frameMode ? bp.frames[existing] : bp.items[existing];

        m_Drag.source = DragSource::Grid;
        m_Drag.id = src.id;
        m_Drag.rotation = src.rotation;
        m_Drag.originalIndex = existing;

        // ?????????????????????????
        const Vector2 origin = CellPosition(src.row, src.col);
        m_Drag.grabOffset = { mousePos.x - origin.x, mousePos.y - origin.y };
    }
    else
    {
        // ---- ???????? ----
        if (!m_HasSelection) return;
        if (ItemDatabase::IsFrame(m_SelectedItem) != frameMode) return;

        if (GetAvailableCount(bp, m_SelectedItem) <= 0) return;

        m_Drag.source = DragSource::Palette;
        m_Drag.id = m_SelectedItem;
        m_Drag.rotation = m_Rotation;
        m_Drag.originalIndex = -1;

        // ?????????????????????????
        const Vector2 origin = CellPosition(row, col);
        m_Drag.grabOffset = { mousePos.x - origin.x, mousePos.y - origin.y };
    }

    UpdateDrag(bp, mousePos);
}

// ============================================================
// ??
//
// ????????????????????
// ?????????????????????????
//
// ????????????????????????????
// ?????????????????????????????
// ????????????????????
// ????????????????
// ============================================================
void BackpackUI::UpdateDrag(BackpackComponent& bp, const Vector2& mousePos)
{
    if (m_Drag.source == DragSource::None) return;

    const float pitch = CellPitch();
    if (pitch <= 0.0f) return;

    // ??????????????(????????)
    const float ox = mousePos.x - m_Drag.grabOffset.x;
    const float oy = mousePos.y - m_Drag.grabOffset.y;

    // ???????????????????
    m_Drag.dropCol = (int)std::lround((ox - m_Origin.x) / pitch);
    m_Drag.dropRow = (int)std::lround((oy - m_Origin.y) / pitch);

    // ????????????????????????????????
    const int ignore = (m_Drag.source == DragSource::Grid) ? m_Drag.originalIndex : -1;

    m_Drag.canDrop = (m_EditMode == EditMode::Frame)
        ? BackpackLogic::CanPlaceFrame(bp, m_Drag.id,
            m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation, ignore)
        : BackpackLogic::CanPlace(bp, m_Drag.id,
            m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation, ignore);
}

// ============================================================
// ???
//
// ???????????????????
// ?????????????????????????
// ???????????????????????????
// ============================================================
void BackpackUI::EndDrag(BackpackComponent& bp)
{
    if (m_Drag.source == DragSource::None) return;

    const bool frameMode = (m_EditMode == EditMode::Frame);

    if (m_Drag.canDrop)
    {
        if (m_Drag.source == DragSource::Grid)
        {
            if (frameMode)
            {
                int evicted = 0;
                BackpackLogic::MoveFrame(bp, m_Drag.originalIndex,
                    m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation, &evicted);
                if (evicted > 0) m_LastEvicted = evicted;
            }
            else
            {
                // ?????????????????????????
                BackpackLogic::Remove(bp, m_Drag.originalIndex);
                BackpackLogic::Place(bp, m_Drag.id,
                    m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation);
            }
        }
        else
        {
            if (frameMode)
                BackpackLogic::PlaceFrame(bp, m_Drag.id,
                    m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation);
            else
                BackpackLogic::Place(bp, m_Drag.id,
                    m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation);
        }
    }

    CancelDrag();

    // ??????????????????????????
    m_HoverItemIndex = m_HoverFrameIndex = -1;
}

// ============================================================
// ?????
//   ?????:????????
//   ?????:??????????????
//   ???? / R:??(??????????)
// ============================================================
void BackpackUI::HandleInput(BackpackComponent& bp)
{
    auto& input = InputManager::Get();

    // ImGui ?????????????????
    if (ImGui::GetIO().WantCaptureMouse)
    {
        // ?????????????(????????????)
        CancelDrag();
        m_HoverRow = m_HoverCol = -1;
        m_HoverItemIndex = m_HoverFrameIndex = -1;
        return;
    }

    // ????????(????????)???
    auto mp = input.GetMousePos();
    m_MousePos = { mp.x, mp.y };

    // ---- ???????????? ----
    if (!ScreenToCell(m_MousePos, m_HoverRow, m_HoverCol))
    {
        m_HoverRow = m_HoverCol = -1;
        m_HoverItemIndex = m_HoverFrameIndex = -1;
    }
    else
    {
        m_HoverItemIndex = BackpackLogic::GetItemAt(bp, m_HoverRow, m_HoverCol);
        m_HoverFrameIndex = BackpackLogic::GetFrameAt(bp, m_HoverRow, m_HoverCol);
    }

    // ---- ??(???? or R ??)----
    // ??????????????????
    float wheel = input.GetMouseWheel();
    int rotDelta = 0;
    if (wheel > 0.0f)      rotDelta = 1;
    else if (wheel < 0.0f) rotDelta = 3;
    if (input.GetKeyTrigger('R')) rotDelta = 1;

    if (rotDelta != 0)
    {
        m_Rotation = (m_Rotation + rotDelta) % 4;
        if (m_Drag.source != DragSource::None)
            m_Drag.rotation = (m_Drag.rotation + rotDelta) % 4;
    }

    // ---- ???? ----
    if (m_Drag.source != DragSource::None)
    {
        UpdateDrag(bp, m_MousePos);

        if (!input.GetMousePress(0))   // ???
            EndDrag(bp);

        return;   // ???????????????
    }

    if (input.GetMouseTrigger(0))
    {
        BeginDrag(bp, m_MousePos);
        return;
    }

    if (m_HoverRow < 0) return;

    // ---- ?????:???? ----
    // ??????????????????????
    // ???????????(?????????????????)?
    if (input.GetMouseTrigger(1))
    {
        if (m_EditMode == EditMode::Frame)
        {
            if (m_HoverFrameIndex >= 0)
                m_LastEvicted = BackpackLogic::RemoveFrame(bp, m_HoverFrameIndex);
        }
        else
        {
            if (m_HoverItemIndex >= 0)
                BackpackLogic::Remove(bp, m_HoverItemIndex);
        }

        m_HoverItemIndex = m_HoverFrameIndex = -1;
    }
}

// ============================================================
// ????????????????
//
// ???????????
// ?????????????????????1?????????
// ?????????????????????????
// ??????????????
// ============================================================
void BackpackUI::DrawHoverInfluence(SpriteRenderer& sprite, const BackpackComponent& bp)
{
    if (!showInfluenceOnHover) return;
    if (m_Drag.source != DragSource::None) return;   // ???????????
    if (m_HoverItemIndex < 0 || m_HoverItemIndex >= (int)bp.items.size()) return;

    const auto& src = bp.items[m_HoverItemIndex];
    const ItemCommon* c = ItemDatabase::GetCommon(src.id);
    if (!c || c->influenceCells.empty()) return;

    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };
    auto cells = BackpackLogic::RotateShape(c->influenceCells, src.rotation);

    Vector4 col = c->color;
    col.w = 0.45f;

    for (const auto& off : cells)
    {
        int r = src.row + off.row;
        int cc = src.col + off.col;
        if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

        sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, col);
    }

    if (!highlightInfluenced) return;

    // ---- ?????????????? ----
    // ??????????????????????
    // ???????????????????????
    // ???????????1????????
    Vector4 glow = { 1.0f, 1.0f, 0.75f, 0.35f };

    for (const auto& off : cells)
    {
        int r = src.row + off.row;
        int cc = src.col + off.col;
        if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

        int target = bp.GetOccupant(r, cc);
        if (target < 0 || target == m_HoverItemIndex) continue;

        const auto& t = bp.items[target];
        const ItemCommon* tc = ItemDatabase::GetCommon(t.id);
        if (!tc) continue;

        auto tcells = BackpackLogic::RotateShape(tc->occupyCells, t.rotation);
        for (const auto& toff : tcells)
            sprite.Draw(m_BlockTex,
                CellPosition(t.row + toff.row, t.col + toff.col),
                cellSizeVec, glow);
    }
}

// ============================================================
// ???????
//
// ???????????????
// ??????????????????????
// ?????????????
// ============================================================
void BackpackUI::DrawDropShadow(SpriteRenderer& sprite)
{
    if (m_Drag.source == DragSource::None) return;

    const ItemCommon* c = ItemDatabase::GetCommon(m_Drag.id);
    if (!c) return;

    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };
    const Vector4 col = m_Drag.canDrop ? Vector4(0.4f, 1.0f, 0.5f, 0.45f)
        : Vector4(1.0f, 0.3f, 0.3f, 0.45f);

    auto cells = BackpackLogic::RotateShape(c->occupyCells, m_Drag.rotation);
    for (const auto& off : cells)
    {
        int r = m_Drag.dropRow + off.row;
        int cc = m_Drag.dropCol + off.col;
        if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

        sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, col);
    }

    // ???????????????????????????
    if (m_EditMode == EditMode::Spell && !c->influenceCells.empty())
    {
        Vector4 ic = c->color;
        ic.w = 0.25f;

        auto infl = BackpackLogic::RotateShape(c->influenceCells, m_Drag.rotation);
        for (const auto& off : infl)
        {
            int r = m_Drag.dropRow + off.row;
            int cc = m_Drag.dropCol + off.col;
            if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

            sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, ic);
        }
    }
}

// ============================================================
// ???????????
//
// ?????????????????
// CellPosition ??????????????????
// ============================================================
void BackpackUI::DrawDragged(SpriteRenderer& sprite, const Vector2& mousePos)
{
    if (m_Drag.source == DragSource::None) return;

    const ItemCommon* c = ItemDatabase::GetCommon(m_Drag.id);
    if (!c) return;

    const float pitch = CellPitch();
    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };

    // ???????????????
    const float ox = mousePos.x - m_Drag.grabOffset.x;
    const float oy = mousePos.y - m_Drag.grabOffset.y;

    auto icon = GetIcon(m_Drag.id);
    auto tex = icon ? icon : m_BlockTex;

    Vector4 col = icon ? Vector4(1, 1, 1, 1) : c->color;
    col.w *= dragAlpha;

    auto cells = BackpackLogic::RotateShape(c->occupyCells, m_Drag.rotation);
    for (const auto& off : cells)
    {
        Vector2 pos = { ox + off.col * pitch, oy + off.row * pitch };
        sprite.Draw(tex, pos, cellSizeVec, col);
    }
}

// ============================================================
// ??
// ??? = ????
//   ??? ? ??(???????)? ???? ? ??????
//   ? ?? ? ??????? ? ???????
// ============================================================
void BackpackUI::Draw(SpriteRenderer& sprite, const BackpackComponent& bp)
{
    if (!m_BlockTex) return;

    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };

    // ---- ??? ----
    float extent = GridExtent();
    Vector2 framePos = { m_Origin.x - m_FramePad, m_Origin.y - m_FramePad };
    Vector2 frameSize = { extent + m_FramePad * 2.0f, extent + m_FramePad * 2.0f };
    sprite.Draw(m_BlockTex, framePos, frameSize, frameColor);

    // ---- ?? ----
    // ??????????????????
    // GRID ??????????????????????????????
    for (int row = 0; row < GRID_SIZE; ++row)
    {
        for (int col = 0; col < GRID_SIZE; ++col)
        {
            const bool placeable = bp.IsPlaceable(row, col);
            sprite.Draw(m_BlockTex, CellPosition(row, col), cellSizeVec,
                placeable ? cellColor : lockedCellColor);
        }
    }

    // ---- ???????????????????? ----
    // ????????????????????????????
    if (m_EditMode == EditMode::Frame && m_Drag.source == DragSource::None &&
        m_HoverFrameIndex >= 0 && m_HoverFrameIndex < (int)bp.frames.size())
    {
        const auto& f = bp.frames[m_HoverFrameIndex];
        const ItemCommon* fc = ItemDatabase::GetCommon(f.id);
        if (fc)
        {
            Vector4 mark = { 1.0f, 0.9f, 0.5f, 0.30f };
            auto cells = BackpackLogic::RotateShape(fc->occupyCells, f.rotation);
            for (const auto& off : cells)
                sprite.Draw(m_BlockTex,
                    CellPosition(f.row + off.row, f.col + off.col),
                    cellSizeVec, mark);
        }
    }

    // ---- ??????(???)----
    // ????????????(????????)????????
    // ??????????????????(??????????)
    const bool draggingFromGrid =
        (m_Drag.source == DragSource::Grid);
    const bool dragIsFrame = (m_EditMode == EditMode::Frame);

    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        if (draggingFromGrid && !dragIsFrame && (int)i == m_Drag.originalIndex)
            continue;

        const auto& item = bp.items[i];
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

    // ---- ????????????(????????)----
    if (m_EditMode == EditMode::Spell)
        DrawHoverInfluence(sprite, bp);

    // ---- ??????? ? ??????? ----
    DrawDropShadow(sprite);
    DrawDragged(sprite, m_MousePos);
}
// ============================================================
// ????????
//
// ????????????????????????
// ??????????????????????????
// ============================================================
int BackpackUI::GetAvailableCount(const BackpackComponent& bp, ItemID id) const
{
    if (!m_Spellbook) return 999;   // ?????????????????

    const int owned = m_Spellbook->GetCount(id);
    const int placed = ItemDatabase::IsFrame(id)
        ? BackpackLogic::CountPlacedFrames(bp, id)
        : BackpackLogic::CountPlaced(bp, id);

    return owned - placed;
}