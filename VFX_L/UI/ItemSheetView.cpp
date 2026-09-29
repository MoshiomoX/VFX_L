// ============================================================
// ItemSheetView.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "UI/ItemSheetView.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Item/ItemDatabase.h"
#include "UI/UIDeco.h"
#include <algorithm>

using namespace DirectX::SimpleMath;

namespace
{
    // 英数字の単語の途中か（ここで折り返すと単語が割れる）
    bool IsWordChar(wchar_t c)
    {
        return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')
            || c == L'.' || c == L'+' || c == L'-' || c == L'/' || c == L'%';
    }

    // 行頭に来てはいけない字（前の行にぶら下げる）
    bool IsNoLineStart(wchar_t c)
    {
        return c == L'、' || c == L'。' || c == L')' || c == L' ';
    }
}

namespace ItemSheetView
{
    Style Style::Scaled(float k) const
    {
        Style s = *this;
        s.titleScale *= k;
        s.bodyScale *= k;
        s.smallScale *= k;
        s.lineGap *= k;
        s.sectionGap *= k;
        s.pad *= k;
        return s;
    }

    std::vector<std::wstring> Wrap(const TextRenderer& text, const std::wstring& s,
        float maxWidth, float scale)
    {
        std::vector<std::wstring> lines;
        std::wstring line;

        for (wchar_t ch : s)
        {
            if (ch == L'\n')
            {
                lines.push_back(line);
                line.clear();
                continue;
            }

            const std::wstring trial = line + ch;
            if (line.empty() || IsNoLineStart(ch) || text.Measure(trial, scale).x <= maxWidth)
            {
                line = trial;
                continue;
            }

            // はみ出す。英単語の途中なら直前の空白で切って、単語ごと次の行へ送る
            const size_t sp = line.rfind(L' ');
            if (IsWordChar(ch) && sp != std::wstring::npos && sp > 0 && IsWordChar(line.back()))
            {
                lines.push_back(line.substr(0, sp));
                line = line.substr(sp + 1) + ch;
            }
            else
            {
                lines.push_back(line);
                line = std::wstring(1, ch);
            }
        }
        if (!line.empty()) lines.push_back(line);

        // 行頭の空白は落とす
        for (auto& l : lines)
        {
            size_t n = 0;
            while (n < l.size() && l[n] == L' ') ++n;
            if (n > 0) l.erase(0, n);
        }
        return lines;
    }

    float DrawBody(SpriteRenderer* sprite, const std::shared_ptr<Texture>& white,
        TextRenderer& text, const ItemInfo::Sheet& sheet,
        const Vector2& pos, float width, const Style& st, bool draw, bool withDescription)
    {
        const float bodyH = text.GetLineHeight(st.bodyScale);
        const float smallH = text.GetLineHeight(st.smallScale);
        float y = pos.y;
        bool any = false;

        // ---- 説明 ----
        if (withDescription && !sheet.description.empty())
        {
            for (const auto& l : Wrap(text, sheet.description, width, st.bodyScale))
            {
                if (draw) text.Draw(l, { pos.x, y }, st.descColor, st.bodyScale);
                y += bodyH + st.lineGap;
            }
            any = true;
        }

        // ---- 特性（飛び方・爆発など）。「・」の後ろで折り返しを揃える ----
        if (!sheet.traits.empty())
        {
            if (any) y += st.sectionGap;
            const std::wstring bullet = L"・";
            const float bw = text.Measure(bullet, st.bodyScale).x;
            for (const auto& t : sheet.traits)
            {
                const auto lines = Wrap(text, t, width - bw, st.bodyScale);
                for (size_t i = 0; i < lines.size(); ++i)
                {
                    if (draw)
                    {
                        if (i == 0) text.Draw(bullet, { pos.x, y }, st.traitColor, st.bodyScale);
                        text.Draw(lines[i], { pos.x + bw, y }, st.traitColor, st.bodyScale);
                    }
                    y += bodyH + st.lineGap;
                }
            }
            any = true;
        }

        // ---- 能力値：名前は左、値は右揃え。変わった値は色を付けて元の値を小さく添える ----
        if (!sheet.stats.empty())
        {
            if (any)
            {
                y += st.sectionGap;
                if (draw && sprite && white)
                    sprite->Draw(white, { pos.x, y - st.sectionGap * 0.5f - 0.5f }, { width, 1.0f }, st.ruleColor);
            }
            for (const auto& l : sheet.stats)
            {
                if (draw)
                {
                    text.Draw(l.label, { pos.x, y }, st.labelColor, st.bodyScale);

                    float x = pos.x + width;
                    if (!l.baseValue.empty())
                    {
                        const std::wstring b = L" (" + l.baseValue + L")";
                        x -= text.Measure(b, st.smallScale).x;
                        text.Draw(b, { x, y + (bodyH - smallH) }, st.dimColor, st.smallScale);
                    }
                    const Vector4 vc = (l.trend > 0) ? st.betterColor
                        : (l.trend < 0) ? st.worseColor : st.textColor;
                    x -= text.Measure(l.value, st.bodyScale).x;
                    text.Draw(l.value, { x, y }, vc, st.bodyScale);
                }
                y += bodyH + st.lineGap;
            }
            any = true;
        }

        // ---- footer（強化の出所・効いている相手）----
        if (!sheet.footer.empty())
        {
            if (any) y += st.sectionGap;
            for (const auto& l : Wrap(text, sheet.footer, width, st.smallScale))
            {
                if (draw) text.Draw(l, { pos.x, y }, st.footerColor, st.smallScale);
                y += smallH + st.lineGap;
            }
        }

        return y - pos.y;
    }

    void DrawTooltip(SpriteRenderer& sprite, const std::shared_ptr<Texture>& white,
        TextRenderer& text, const ItemInfo::Sheet& sheet,
        const Vector2& anchor, const Vector2& screen, float width, const Style& st)
    {
        const float inner = width - st.pad * 2.0f;
        const float titleH = text.GetLineHeight(st.titleScale);
        const float smallH = text.GetLineHeight(st.smallScale);
        const float headGap = st.sectionGap * 0.75f;

        const float bodyH = DrawBody(nullptr, white, text, sheet, { 0.0f, 0.0f }, inner, st, false);
        const float h = st.pad + titleH + headGap + bodyH + st.pad;

        // ---- 位置：マウスの右下。はみ出すなら左 / 上へ ----
        const float off = 18.0f;
        const float margin = 4.0f;
        Vector2 p = { anchor.x + off, anchor.y + off };
        if (p.x + width > screen.x - margin) p.x = anchor.x - off - width;
        if (p.y + h > screen.y - margin)     p.y = screen.y - margin - h;
        p.x = (std::max)(p.x, margin);
        p.y = (std::max)(p.y, margin);

        // ---- 箱（幻想 UI：道具の種類の色の二重線）----
        const Vector4 tint = UIDeco::CategoryColor(ItemDatabase::GetCategory(sheet.id));
        UIDeco::PanelStyle ps;
        ps.fill = st.panelColor;
        ps.innerInset = 3.0f;
        UIDeco::DrawPanel(sprite, p, { width, h }, tint, ps);

        // ---- 名前（左）と種別（右）----
        const float x = p.x + st.pad;
        float y = p.y + st.pad;
        text.Draw(sheet.title, { x, y }, st.textColor, st.titleScale);
        const float cw = text.Measure(sheet.category, st.smallScale).x;
        text.Draw(sheet.category, { p.x + width - st.pad - cw, y + (titleH - smallH) }, tint, st.smallScale);
        y += titleH + headGap;

        DrawBody(&sprite, white, text, sheet, { x, y }, inner, st, true);
    }
}
