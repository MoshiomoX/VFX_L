// ============================================================
// BackpackUIOutlet.cpp
// バックパックの出口マスの表示（2026-10-31）。置いてある出口魔法は常に出す（マウスを乗せなくても繋がりが読める）。
// BackpackUI.cpp が 600 行を超えているので分けた
// ============================================================
#include "UI/BackpackUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "UI/OutletDraw.h"
#include "UI/UIDeco.h"

using namespace DirectX::SimpleMath;

void BackpackUI::DrawOutlets(SpriteRenderer& sprite, const BackpackComponent& bp)
{
    const auto& white = UIDeco::Tex().white ? UIDeco::Tex().white : m_BlockTex;
    const int hidden = (m_Drag && m_Drag->IsActive()) ? m_Drag->originalIndex : -1;
    const auto edges = BackpackLogic::GetOutletEdges(bp);   // 上流へ戻る出口は入っていない = 薄く出る

    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        if ((int)i == hidden) continue;   // 掴んでいる物は DrawDragOutlets が置き先に出す
        const auto& item = bp.items[i];
        const ItemCommon* c = ItemDatabase::GetCommon(item.id);
        if (!c || c->outletCells.empty()) continue;

        const auto occupy = BackpackLogic::RotateShape(c->occupyCells, item.rotation);
        for (const auto& off : BackpackLogic::RotateShape(c->outletCells, item.rotation))
        {
            const int r = item.row + off.row, col = item.col + off.col;
            if (r < 0 || r >= GRID_SIZE || col < 0 || col >= GRID_SIZE) continue;
            int dRow = 0, dCol = 0;
            OutletDraw::Direction(occupy, off, dRow, dCol);
            const int t = bp.GetOccupant(r, col);
            const bool connected = t >= 0 && t != hidden && std::find(edges[i].begin(), edges[i].end(), t) != edges[i].end();
            OutletDraw::Cell(sprite, white, CellPosition(r, col), m_CellSize, dRow, dCol, connected);
        }
    }
}

void BackpackUI::DrawDragOutlets(SpriteRenderer& sprite)
{
    if (!m_Drag || !m_Drag->IsActive()) return;
    const ItemCommon* c = ItemDatabase::GetCommon(m_Drag->id);
    if (!c || c->outletCells.empty()) return;

    const auto& white = UIDeco::Tex().white ? UIDeco::Tex().white : m_BlockTex;
    const auto occupy = BackpackLogic::RotateShape(c->occupyCells, m_Drag->rotation);
    for (const auto& off : BackpackLogic::RotateShape(c->outletCells, m_Drag->rotation))
    {
        const int r = m_Drag->dropRow + off.row, col = m_Drag->dropCol + off.col;
        if (r < 0 || r >= GRID_SIZE || col < 0 || col >= GRID_SIZE) continue;
        int dRow = 0, dCol = 0;
        OutletDraw::Direction(occupy, off, dRow, dCol);
        OutletDraw::Cell(sprite, white, CellPosition(r, col), m_CellSize, dRow, dCol, false);
    }
}
