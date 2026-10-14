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

    // 形の外接矩形の中心に一番近いマス（アイコンを 1 つだけ置く場所）。cells は空でない前提
    inline CellOffset CenterCell(const std::vector<CellOffset>& cells)
    {
        int minR = cells[0].row, maxR = cells[0].row, minC = cells[0].col, maxC = cells[0].col;
        for (const auto& o : cells)
        {
            minR = (std::min)(minR, o.row); maxR = (std::max)(maxR, o.row);
            minC = (std::min)(minC, o.col); maxC = (std::max)(maxC, o.col);
        }
        const float cr = (minR + maxR) * 0.5f;
        const float cc = (minC + maxC) * 0.5f;
        CellOffset best = cells[0];
        float bestD = 1.0e9f;
        for (const auto& o : cells)
        {
            const float d = (o.row - cr) * (o.row - cr) + (o.col - cc) * (o.col - cc);
            if (d < bestD) { bestD = d; best = o; }
        }
        return best;
    }

    // 形の外周だけに線を引く（隙間を塗ってつないだ 1 枚の輪郭）。
    // マスの辺は隣が無ければ引く。隣とつながる隙間の辺は、左 / 上のマスの担当にして二重に引かない
    inline void DrawOutline(SpriteRenderer& sprite,
        const std::shared_ptr<Texture>& lineTex, const DirectX::SimpleMath::Vector4& color,
        const std::vector<CellOffset>& cells,
        const DirectX::SimpleMath::Vector2& origin, float cell, float gap, float th = 1.0f)
    {
        auto has = [&](int r, int c)
            {
                return std::any_of(cells.begin(), cells.end(),
                    [&](const CellOffset& o) { return o.row == r && o.col == c; });
            };
        const float pitch = cell + gap;
        for (const auto& o : cells)
        {
            const float x = origin.x + (float)o.col * pitch;
            const float y = origin.y + (float)o.row * pitch;
            const int r = o.row, c = o.col;
            // 辺
            if (!has(r - 1, c)) sprite.Draw(lineTex, { x, y }, { cell, th }, color);
            if (!has(r + 1, c)) sprite.Draw(lineTex, { x, y + cell - th }, { cell, th }, color);
            if (!has(r, c - 1)) sprite.Draw(lineTex, { x, y }, { th, cell }, color);
            if (!has(r, c + 1)) sprite.Draw(lineTex, { x + cell - th, y }, { th, cell }, color);
            if (gap <= 0.0f) continue;
            // 右の隙間（右とつながる時）の上下の辺。上下の 2x2 が全部埋まっていれば中の角も塗られているので引かない
            if (has(r, c + 1))
            {
                if (!(has(r - 1, c) && has(r - 1, c + 1))) sprite.Draw(lineTex, { x + cell, y }, { gap, th }, color);
                if (!(has(r + 1, c) && has(r + 1, c + 1))) sprite.Draw(lineTex, { x + cell, y + cell - th }, { gap, th }, color);
            }
            // 下の隙間（下とつながる時）の左右の辺
            if (has(r + 1, c))
            {
                if (!(has(r, c - 1) && has(r + 1, c - 1))) sprite.Draw(lineTex, { x, y + cell }, { th, gap }, color);
                if (!(has(r, c + 1) && has(r + 1, c + 1))) sprite.Draw(lineTex, { x + cell - th, y + cell }, { th, gap }, color);
            }
        }
    }

    // 幻想 UI のアイテムの見た目：アイテムの色を暗く沈めた「色ガラス」+ 種類の色の輪郭 + 暖かい白のシルエット
    inline constexpr float kGlassDim = 0.12f;     // アイテムの色をどれだけ暗くして地にするか（線形）
    inline constexpr float kGlassAlpha = 0.92f;
    inline const DirectX::SimpleMath::Vector4 kIconTint = { 0.97f, 0.94f, 0.87f, 1.0f };
    inline constexpr float kIconScale = 0.88f;    // アイコンの大きさ（マス比。2026-09-30 0.74 → 0.88、ユーザー要求）

    // アイテム 1 個（幻想 UI）。white は無地の白、outline は種類の色（UIDeco::CategoryColor）
    inline void DrawItemGlass(SpriteRenderer& sprite,
        const std::shared_ptr<Texture>& white, const DirectX::SimpleMath::Vector4& color,
        const DirectX::SimpleMath::Vector4& outline,
        const std::shared_ptr<Texture>& icon, const std::vector<CellOffset>& cells,
        const DirectX::SimpleMath::Vector2& origin, float cell, float gap, float alpha = 1.0f)
    {
        if (cells.empty()) return;

        DirectX::SimpleMath::Vector4 base = { color.x * kGlassDim, color.y * kGlassDim, color.z * kGlassDim,
            kGlassAlpha * alpha };
        DrawConnected(sprite, white, base, cells, origin, cell, gap);

        DirectX::SimpleMath::Vector4 line = outline;
        line.w = 0.95f * alpha;
        DrawOutline(sprite, white, line, cells, origin, cell, gap, (std::max)(1.0f, cell * 0.035f));

        if (!icon) return;
        const CellOffset c = CenterCell(cells);
        const float pitch = cell + gap;
        const float s = cell * kIconScale;
        DirectX::SimpleMath::Vector4 ic = kIconTint;
        ic.w = alpha;
        sprite.Draw(icon,
            { origin.x + (float)c.col * pitch + (cell - s) * 0.5f, origin.y + (float)c.row * pitch + (cell - s) * 0.5f },
            { s, s }, ic);
    }

    // アイコンを重ねる時の、地のブロックの暗さ（アイコンを浮かせる）
    inline constexpr float kIconBaseDim = 0.55f;

    // アイテム 1 個。形はアイテムの色のブロック（アイコンがある時は暗くして地にする）、
    // アイコンは中心のマスに 1 つだけ重ねる（マスごとに貼ると多マスの物が同じ絵の繰り返しになる）
    inline void DrawItem(SpriteRenderer& sprite,
        const std::shared_ptr<Texture>& blockTex, const DirectX::SimpleMath::Vector4& color,
        const std::shared_ptr<Texture>& icon, const std::vector<CellOffset>& cells,
        const DirectX::SimpleMath::Vector2& origin, float cell, float gap, float alpha = 1.0f)
    {
        if (cells.empty()) return;

        DirectX::SimpleMath::Vector4 base = color;
        if (icon)
        {
            base.x *= kIconBaseDim;
            base.y *= kIconBaseDim;
            base.z *= kIconBaseDim;
        }
        base.w *= alpha;
        DrawConnected(sprite, blockTex, base, cells, origin, cell, gap);

        if (!icon) return;
        const CellOffset c = CenterCell(cells);
        const float pitch = cell + gap;
        const float s = cell * 0.92f;
        sprite.Draw(icon,
            { origin.x + (float)c.col * pitch + (cell - s) * 0.5f, origin.y + (float)c.row * pitch + (cell - s) * 0.5f },
            { s, s }, { 1.0f, 1.0f, 1.0f, alpha });
    }
}
