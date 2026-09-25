// ============================================================
// ShapeSprite.h
// 異形ブロックを「つながった 1 枚」として描く。
//
// マスを 1 個ずつ描くと、隣り合う 1 マスの物 2 個と
// 2 マスの物 1 個の見分けが付かない。
// 同じ物の隣り合うマスの間（隙間）も塗って 1 枚に見せる:
//   横に隣接           → 縦長の隙間を塗る
//   縦に隣接           → 横長の隙間を塗る
//   2x2 が全部埋まる   → 真ん中の小さい隙間も塗る（塗らないと穴が見える）
// 隙間とマスは重ならないので、半透明で描いても濃さがむらにならない。
// ============================================================
#pragma once
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Item/ItemTypes.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace ShapeSprite
{
    // cells はアンカー相対（回転済み）。origin はアンカーのマスの左上。
    //   マス (r, c) の左上 = origin + (c, r) * (cell + gap)
    // 隙間は fillTex / fillColor で塗る。
    //   マスにアイコンを貼る時、隙間までアイコンを細く引き伸ばさないように分けてある
    inline void DrawConnected(SpriteRenderer& sprite,
        const std::shared_ptr<Texture>& cellTex, const DirectX::SimpleMath::Vector4& cellColor,
        const std::shared_ptr<Texture>& fillTex, const DirectX::SimpleMath::Vector4& fillColor,
        const std::vector<CellOffset>& cells,
        const DirectX::SimpleMath::Vector2& origin, float cell, float gap)
    {
        auto has = [&](int r, int c)
            {
                return std::any_of(cells.begin(), cells.end(),
                    [&](const CellOffset& o) { return o.row == r && o.col == c; });
            };

        const float pitch = cell + gap;
        for (const auto& o : cells)
        {
            const DirectX::SimpleMath::Vector2 p = {
                origin.x + (float)o.col * pitch,
                origin.y + (float)o.row * pitch
            };
            sprite.Draw(cellTex, p, { cell, cell }, cellColor);

            if (gap <= 0.0f) continue;
            const bool right = has(o.row, o.col + 1);
            const bool down = has(o.row + 1, o.col);
            if (right) sprite.Draw(fillTex, { p.x + cell, p.y }, { gap, cell }, fillColor);
            if (down)  sprite.Draw(fillTex, { p.x, p.y + cell }, { cell, gap }, fillColor);
            if (right && down && has(o.row + 1, o.col + 1))
                sprite.Draw(fillTex, { p.x + cell, p.y + cell }, { gap, gap }, fillColor);
        }
    }

    // マスも隙間も同じテクスチャ・色で塗る（アイコン無しの普通の場合）
    inline void DrawConnected(SpriteRenderer& sprite,
        const std::shared_ptr<Texture>& tex, const DirectX::SimpleMath::Vector4& color,
        const std::vector<CellOffset>& cells,
        const DirectX::SimpleMath::Vector2& origin, float cell, float gap)
    {
        DrawConnected(sprite, tex, color, tex, color, cells, origin, cell, gap);
    }
}
