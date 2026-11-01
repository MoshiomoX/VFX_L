// ============================================================
// OutletDraw.h
// 出口マスの絵（2026-10-31）。バックパックとカードで共用。
// 金の枠と、出口の向きの矢印。繋がっている時は濃く出す
// ============================================================
#pragma once
#include "Graphics/Material/Texture.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Item/ItemTypes.h"
#include "UI/UIDeco.h"
#include <SimpleMath.h>
#include <memory>
#include <vector>

namespace OutletDraw
{
    // 出口の向き：形の中の隣のマス → 出口マス（マスの単位。隣が無ければ右）
    inline void Direction(const std::vector<CellOffset>& occupy, const CellOffset& outlet, int& dRow, int& dCol)
    {
        static const int kDir[4][2] = { { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 } };
        for (const auto& d : kDir)
            for (const CellOffset& o : occupy)
                if (o.row == outlet.row - d[0] && o.col == outlet.col - d[1])
                {
                    dRow = d[0];
                    dCol = d[1];
                    return;
                }
        dRow = 0;
        dCol = 1;
    }

    // 向き (dRow, dCol) の三角の矢印。回す描画が無いので細い帯を積む
    inline void Arrow(SpriteRenderer& sprite, const std::shared_ptr<Texture>& white,
        const DirectX::SimpleMath::Vector2& center, float size, int dRow, int dCol,
        const DirectX::SimpleMath::Vector4& color)
    {
        constexpr int kSteps = 7;
        const float step = size / (float)kSteps;
        for (int i = 0; i < kSteps; ++i)
        {
            const float half = size * 0.5f * (1.0f - ((float)i + 0.5f) / (float)kSteps);
            const float along = -size * 0.5f + step * (float)i;
            if (dCol != 0)
            {
                const float x = dCol > 0 ? center.x + along : center.x - along - step;
                sprite.Draw(white, { x, center.y - half }, { step + 0.5f, half * 2.0f }, color);
            }
            else
            {
                const float y = dRow > 0 ? center.y + along : center.y - along - step;
                sprite.Draw(white, { center.x - half, y }, { half * 2.0f, step + 0.5f }, color);
            }
        }
    }

    // 出口マス 1 つ（pos = マスの左上）。connected = 先に魔法が乗っている
    inline void Cell(SpriteRenderer& sprite, const std::shared_ptr<Texture>& white,
        const DirectX::SimpleMath::Vector2& pos, float cell, int dRow, int dCol, bool connected)
    {
        const DirectX::SimpleMath::Vector4 fill = { 1.0f, 0.70f, 0.20f, connected ? 0.20f : 0.12f };
        const DirectX::SimpleMath::Vector4 edge = { 1.0f, 0.78f, 0.30f, connected ? 0.95f : 0.60f };
        const DirectX::SimpleMath::Vector4 mark = { 1.0f, 0.85f, 0.40f, connected ? 1.0f : 0.70f };
        sprite.Draw(white, pos, { cell, cell }, fill);
        UIDeco::DrawFrameLines(sprite, pos, { cell, cell }, edge);
        Arrow(sprite, white, { pos.x + cell * 0.5f, pos.y + cell * 0.5f }, cell * 0.52f, dRow, dCol, mark);
    }
}
