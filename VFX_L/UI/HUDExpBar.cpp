// ============================================================
// HUDExpBar.cpp
// HUD の経験値バー（2026-10-15 ユーザー：「経験値バーが見にくい」→ 最上段の通しのまま強める）。
//   暗い下敷き + 古金の枠、中身は明るい金（上半分を少し明るく、下 4 割を暗く）、
//   先端に光る点、真ん中に「次のレベルまで 今 / 必要」、経験値を拾った瞬間に明るく脈打ち、
//   レベルが上がった瞬間はバー全体が白く光って戻る。
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する。HUD.cpp が長いので別ファイル
// ============================================================
#include "UI/HUD.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Player/LevelComponent.h"

using namespace DirectX::SimpleMath;

namespace
{
    float Clamp01(float v) { return (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v; }
}

void HUD::DrawExpBar(SpriteRenderer& sprite, TextRenderer& text, const LevelComponent& lv)
{
    // ---- 変化の検出（拾った → 脈、上がった → 全体の光）。Draw は毎フレーム呼ばれるのでここで ----
    if (m_LastLevel >= 0 && lv.level > m_LastLevel) m_ExpFlashAt = m_Time;
    else if (m_LastExp >= 0.0f && lv.experience > m_LastExp + 0.01f) m_ExpPulseAt = m_Time;
    m_LastLevel = lv.level;
    m_LastExp = lv.experience;
    const float pulse = std::exp(-(m_Time - m_ExpPulseAt) * 5.0f);   // 1 → 0（約 0.5 秒）
    const float flash = Clamp01(1.0f - (m_Time - m_ExpFlashAt) / 0.6f);

    const Vector2 pos = m_S.expBar.Resolve(m_ScreenW, m_ScreenH) + Vector2(m_S.expBarMargin, 0.0f);
    const Vector2 size = { m_ScreenW - m_S.expBarMargin * 2.0f, m_S.expBarHeight };
    // 選択待ちで持ち越し中は 1.0 を超えるので丸める
    const float ratio = Clamp01(lv.Progress());

    // ---- 下敷き（枠を兼ねる暗い地）----
    sprite.Draw(m_WhiteTex, pos, size, m_S.bgColor);

    const float pad = m_S.barPadding;
    const Vector2 innerPos = { pos.x + pad, pos.y + pad };
    const Vector2 innerSize = { size.x - pad * 2.0f, size.y - pad * 2.0f };
    if (innerSize.x <= 0.0f || innerSize.y <= 0.0f) return;

    // ---- 中身：金。拾った瞬間は明るく ----
    const float w = innerSize.x * ratio;
    if (w > 0.0f)
    {
        Vector4 fill = m_S.expColor;
        const float gain = 1.0f + 0.6f * pulse;
        fill.x *= gain; fill.y *= gain; fill.z *= gain;
        sprite.Draw(m_WhiteTex, innerPos, { w, innerSize.y }, fill);
        // 上 4 割を少し明るく、下 4 割を暗く（厚み）
        sprite.Draw(m_WhiteTex, innerPos, { w, innerSize.y * 0.4f }, { 1.0f, 0.95f, 0.75f, 0.18f });
        sprite.Draw(m_WhiteTex, { innerPos.x, innerPos.y + innerSize.y * 0.6f }, { w, innerSize.y * 0.4f },
            { 0.0f, 0.0f, 0.0f, 0.28f });

        // 先端の光（短い帯を 3 段、先端ほど濃く）+ 細い白線
        if (m_S.expTipGlow)
        {
            const float tipW = innerSize.y * 2.5f;
            for (int i = 0; i < 3; ++i)
            {
                const float f = (float)(i + 1) / 3.0f;
                const float gw = (std::min)(tipW * f, w);
                sprite.Draw(m_WhiteTex, { innerPos.x + w - gw, innerPos.y }, { gw, innerSize.y },
                    { 1.0f, 0.92f, 0.60f, 0.16f + 0.10f * pulse });
            }
            const float lineW = (std::max)(1.0f, innerSize.y * 0.12f);
            sprite.Draw(m_WhiteTex, { innerPos.x + w - lineW, innerPos.y }, { lineW, innerSize.y },
                { 1.2f, 1.1f, 0.85f, 0.85f });
        }
    }

    // ---- レベルが上がった瞬間：バー全体が白く光って戻る ----
    if (flash > 0.0f)
        sprite.Draw(m_WhiteTex, innerPos, innerSize, { 1.3f, 1.2f, 0.9f, 0.75f * flash * flash });

    // ---- 枠 ----
    if (m_S.drawBorder) DrawBorder(sprite, pos, size);

    // ---- 真ん中の文字：次のレベルまで 今 / 必要 ----
    if (m_S.expShowText)
    {
        wchar_t buf[64];
        swprintf_s(buf, L"次のレベルまで　%d / %d", (int)lv.experience, (int)lv.ExpToNext());
        const float s = m_S.expTextScale;
        const Vector2 sz = text.Measure(buf, s);
        // 金の中身の上では読めないので、バーの高さに収まる暗い下敷きを文字の幅だけ敷く
        const float padX = innerSize.y * 0.6f;
        sprite.Draw(m_WhiteTex, { pos.x + (size.x - sz.x) * 0.5f - padX, innerPos.y },
            { sz.x + padX * 2.0f, innerSize.y }, { 0.0f, 0.0f, 0.0f, 0.62f });
        DrawLabel(text, buf, { pos.x + (size.x - sz.x) * 0.5f, pos.y + (size.y - sz.y) * 0.5f }, s);
    }
}
