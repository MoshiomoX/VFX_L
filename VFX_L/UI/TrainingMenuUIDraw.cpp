// ============================================================
// TrainingMenuUIDraw.cpp
// トレーニングのメニューの描画：暗幕 → 箱 → 見出し → 行 → 下の 2 行（選んでいる物の説明 / 結果、操作の案内）
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "UI/TrainingMenuUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Component/SpellbookComponent.h"
#include "Item/ItemDatabase.h"
#include "Item/ItemInfo.h"
#include "UI/UIDeco.h"

using namespace DirectX::SimpleMath;

namespace
{
    const Vector4 kTextSel = { 0.96f, 0.92f, 0.84f, 1.0f };
    const Vector4 kTextDim = { 0.50f, 0.46f, 0.40f, 1.0f };

    // 幅に収まる文字の倍率（長いボタンの名前は小さくする）
    float FitText(TextRenderer& text, const std::wstring& s, float scale, float maxW)
    {
        const float w = text.Measure(s, scale).x;
        return (w > maxW && w > 0.0f) ? scale * maxW / w : scale;
    }
}

void TrainingMenuUI::Draw(SpriteRenderer& sprite, TextRenderer& text, const std::shared_ptr<Texture>& white,
    const SpellbookComponent* book)
{
    if (!white || m_Rows.empty()) return;

    sprite.Draw(white, { 0.0f, 0.0f }, m_Screen, dimColor);

    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    {
        UIDeco::PanelStyle ps;
        ps.fill = panelColor;
        ps.innerInset = (std::max)(4.0f, m_Short * 0.007f);
        ps.cornerSize = m_Short * 0.10f;
        UIDeco::DrawPanel(sprite, m_PanelPos, m_PanelSize, gold, ps);
    }

    const float s = m_Short * m_Scale;
    const float k = m_Short / 900.0f * m_Scale;
    const float pad = s * kPadRatio;
    const float rowW = m_PanelSize.x - pad * 2.0f;

    // ---- 見出し ----
    const std::wstring title = L"トレーニング";
    const float titleScale = 0.85f * k;
    const Vector2 ts = text.Measure(title, titleScale);
    const float titleH = s * kTitleRatio;
    const float titleY = m_PanelPos.y + pad + (titleH - ts.y) * 0.5f - titleH * 0.2f;
    text.Draw(title, { m_PanelPos.x + (m_PanelSize.x - ts.x) * 0.5f, titleY }, kTextSel, titleScale);
    UIDeco::DrawDivider(sprite, true, { m_PanelPos.x + m_PanelSize.x * 0.5f, titleY + ts.y + titleH * 0.08f },
        m_PanelSize.x * 0.55f, gold);

    // ---- 行 ----
    const float labelScale = 0.44f * k;
    const float buttonScale = 0.38f * k;
    for (int i = 0; i < (int)m_Rows.size(); ++i)
    {
        const Row& r = m_Rows[i];
        const bool selRow = (i == m_Row);
        const Vector2 p = RowPos(i);
        const float h = RowHeight(i);
        const float cy = p.y + h * 0.5f;

        // 行の地（絵の行は地を敷かず、絵ごとに枠）
        if (r.kind != RowKind::Items)
        {
            UIDeco::PanelStyle ps;
            ps.fill = selRow ? Vector4(0.010f, 0.008f, 0.016f, 0.96f) : Vector4(0.003f, 0.0025f, 0.005f, 0.90f);
            ps.innerInset = 3.0f;
            ps.innerAlpha = selRow ? 0.45f : 0.20f;
            Vector4 line = gold;
            if (!selRow) line.w = 0.55f;
            UIDeco::DrawPanel(sprite, p, { rowW, h }, line, ps, (selRow && r.kind != RowKind::Buttons) ? 1.0f : 0.0f);
        }

        const Vector4 textCol = selRow ? kTextSel : kTextDim;
        if (r.kind == RowKind::Back)
        {
            const Vector2 ls = text.Measure(r.label, labelScale);
            text.Draw(r.label, { p.x + (rowW - ls.x) * 0.5f, cy - ls.y * 0.5f }, textCol, labelScale);
            continue;
        }
        if (!r.label.empty())
        {
            const Vector2 ls = text.Measure(r.label, labelScale);
            const float ly = (r.kind == RowKind::Items) ? p.y + m_RowH * 0.5f - ls.y * 0.5f : cy - ls.y * 0.5f;
            text.Draw(r.label, { p.x + m_RowH * 0.35f, ly }, textCol, labelScale);
        }

        switch (r.kind)
        {
        case RowKind::Buttons:
            for (int c = 0; c < (int)r.buttons.size(); ++c)
            {
                const bool sel = selRow && c == m_Col;
                Vector2 bp, bs;
                CellRect(i, c, bp, bs);
                const float inset = bs.y * 0.12f;
                bp.y += inset; bs.y -= inset * 2.0f;
                UIDeco::PanelStyle ps;
                ps.fill = sel ? Vector4(0.020f, 0.015f, 0.030f, 0.98f) : Vector4(0.004f, 0.003f, 0.007f, 0.95f);
                ps.innerInset = 2.0f;
                ps.innerAlpha = sel ? 0.5f : 0.15f;
                Vector4 line = gold;
                line.w = sel ? 1.0f : 0.40f;
                UIDeco::DrawPanel(sprite, bp, bs, line, ps, sel ? 1.0f : 0.0f);
                const float sc = FitText(text, r.buttons[c], buttonScale, bs.x * 0.9f);
                const Vector2 ls = text.Measure(r.buttons[c], sc);
                text.Draw(r.buttons[c], { bp.x + (bs.x - ls.x) * 0.5f, bp.y + (bs.y - ls.y) * 0.5f },
                    sel ? kTextSel : kTextDim, sc);
            }
            break;

        case RowKind::Choice:
        case RowKind::Toggle:
        {
            Vector2 cp, cs;
            ControlRect(i, cp, cs);
            const float arrowW = cs.y;
            const Vector4 arrowCol = selRow ? gold : Vector4(gold.x, gold.y, gold.z, 0.45f);
            for (const auto& a : { std::make_pair(cp.x + arrowW * 0.5f, L"<"), std::make_pair(cp.x + cs.x - arrowW * 0.5f, L">") })
            {
                const Vector2 as = text.Measure(a.second, labelScale);
                text.Draw(a.second, { a.first - as.x * 0.5f, cy - as.y * 0.5f }, arrowCol, labelScale);
            }
            const std::wstring v = ValueText(r);
            const Vector2 vs = text.Measure(v, labelScale);
            Vector4 vc = textCol;
            if (r.kind == RowKind::Toggle && !m_Moving) { vc.x *= 0.75f; vc.y *= 0.75f; vc.z *= 0.75f; }
            text.Draw(v, { cp.x + (cs.x - vs.x) * 0.5f, cy - vs.y * 0.5f }, vc, labelScale);
            break;
        }

        case RowKind::Items:
            for (int c = 0; c < (int)r.items.size(); ++c)
            {
                const ItemID id = r.items[c];
                const bool sel = selRow && c == m_Col;
                Vector2 ip, is;
                CellRect(i, c, ip, is);
                const ItemCommon* ic = ItemDatabase::GetCommon(id);
                const Vector4 cat = UIDeco::CategoryColor(ItemDatabase::GetCategory(id));

                // 道具の色を暗くした地 + 種類の色の枠 + 白い絵
                Vector4 base = ic ? ic->color : Vector4(0.5f, 0.5f, 0.5f, 1.0f);
                base = Vector4(base.x * 0.22f, base.y * 0.22f, base.z * 0.22f, 0.96f);
                UIDeco::PanelStyle ps;
                ps.fill = base;
                ps.innerInset = 2.0f;
                ps.innerAlpha = sel ? 0.6f : 0.2f;
                Vector4 line = sel ? gold : cat;
                line.w = sel ? 1.0f : 0.55f;
                UIDeco::DrawPanel(sprite, ip, is, line, ps, sel ? 1.0f : 0.0f);
                if (auto icon = m_IconLookup ? m_IconLookup(id) : nullptr)
                {
                    const float isz = is.x * 0.70f;
                    sprite.Draw(icon, { ip.x + (is.x - isz) * 0.5f, ip.y + (is.y - isz) * 0.5f }, { isz, isz },
                        sel ? kTextSel : Vector4(0.80f, 0.78f, 0.74f, 1.0f));
                }
                // 持っている数（右下）
                if (book)
                {
                    const int have = book->GetCount(id);
                    if (have > 0)
                    {
                        const std::wstring num = L"x" + std::to_wstring(have);
                        const float ns = 0.30f * k;
                        const Vector2 sz = text.Measure(num, ns);
                        text.Draw(num, { ip.x + is.x - sz.x - is.x * 0.06f, ip.y + is.y - sz.y }, kTextSel, ns);
                    }
                }
            }
            break;

        default:
            break;
        }
    }

    // ---- 下の 2 行：選んでいる物の説明（無ければ直前の結果）、操作の案内 ----
    const int n = (int)m_Rows.size();
    const Vector2 lastPos = RowPos(n - 1);
    const float footTop = lastPos.y + RowHeight(n - 1);
    const float footH = m_PanelPos.y + m_PanelSize.y - pad - footTop;

    std::wstring info;
    const ItemID focused = FocusedItem();
    if (focused != ItemID::Unknown)
    {
        const ItemCommon* ic = ItemDatabase::GetCommon(focused);
        if (ic)
        {
            info = ic->displayName;
            info += L"（";
            info += ItemInfo::CategoryLabel(ItemDatabase::GetCategory(focused));
            info += L"）　決定で木箱に 1 個入れる";
            if (book) info += L"　所持 " + std::to_wstring(book->GetCount(focused));
        }
    }
    else if (m_MessageTime > 0.0f)
        info = m_Message;
    if (!info.empty())
    {
        const float sc = FitText(text, info, 0.38f * k, rowW);
        const Vector2 is = text.Measure(info, sc);
        text.Draw(info, { m_PanelPos.x + (m_PanelSize.x - is.x) * 0.5f, footTop + footH * 0.30f - is.y * 0.5f },
            (focused != ItemID::Unknown) ? kTextSel : Vector4(1.0f, 0.80f, 0.40f, 1.0f), sc);
    }

    const std::wstring hint = L"↑↓ で選択　← → で変更　Enter / パッド A で決定　T / Esc / パッド B・RB で閉じる";
    const float hsc = FitText(text, hint, 0.32f * k, rowW);
    const Vector2 hs = text.Measure(hint, hsc);
    text.Draw(hint, { m_PanelPos.x + (m_PanelSize.x - hs.x) * 0.5f, footTop + footH * 0.75f - hs.y * 0.5f },
        { 0.6f, 0.6f, 0.65f, 1.0f }, hsc);
}
