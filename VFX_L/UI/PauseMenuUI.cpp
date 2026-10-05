// ============================================================
// PauseMenuUI.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "UI/PauseMenuUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Manager/InputManager.h"
#include "UI/UIDeco.h"
#include <algorithm>
#include <string>

using namespace DirectX::SimpleMath;

namespace
{
    // 余白・見出し・案内の高さ（画面短辺に対する比率）
    constexpr float kPadRatio = 0.04f;
    constexpr float kTitleRatio = 0.10f;
    constexpr float kHintRatio = 0.07f;
}

PauseMenuUI::PauseMenuUI()
{
    m_List.SetItems({ L"再開する", L"最初からやり直す", L"タイトルへ戻る" });
}

void PauseMenuUI::Layout(float screenW, float screenH)
{
    m_Screen = { screenW, screenH };
    m_Short = (std::min)(screenW, screenH);

    const float pad = m_Short * kPadRatio;
    const float itemH = m_Short * itemHeightRatio;
    const float gap = m_Short * itemGapRatio;
    const float listH = itemH * (float)m_List.Count() + gap * (float)(m_List.Count() - 1);

    m_PanelSize = { m_Short * panelWidthRatio,
        pad + m_Short * kTitleRatio + listH + m_Short * kHintRatio + pad };
    m_PanelPos = { (screenW - m_PanelSize.x) * 0.5f, (screenH - m_PanelSize.y) * 0.5f };

    m_List.Layout({ m_PanelPos.x + pad, m_PanelPos.y + pad + m_Short * kTitleRatio },
        { m_PanelSize.x - pad * 2.0f, itemH }, gap);
    m_List.accentColor = accentColor;
}

PauseMenuUI::Action PauseMenuUI::HandleInput()
{
    // パッドの B は「戻る」の決まりごとなので、カーソルに関係なく再開
    if (InputManager::Get().GetPadTrigger(XINPUT_GAMEPAD_B))
        return Action::Resume;

    switch (m_List.HandleInput())
    {
    case 0:  return Action::Resume;
    case 1:  return Action::Restart;
    case 2:  return Action::Title;
    default: return Action::None;
    }
}

// ============================================================
// 描画：暗幕 → 箱 → 見出し → 項目 → 操作の案内
// ============================================================
void PauseMenuUI::Draw(SpriteRenderer& sprite, TextRenderer& text, const std::shared_ptr<Texture>& white)
{
    if (!white) return;

    sprite.Draw(white, { 0.0f, 0.0f }, m_Screen, dimColor);

    // ---- パネル（幻想 UI：古金の二重線 + 四隅の組紐）----
    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    {
        UIDeco::PanelStyle ps;
        ps.fill = panelColor;
        ps.innerInset = (std::max)(4.0f, m_Short * 0.007f);
        ps.cornerSize = m_Short * 0.10f;
        UIDeco::DrawPanel(sprite, m_PanelPos, m_PanelSize, gold, ps);
    }

    const float k = m_Short / 900.0f;
    const float pad = m_Short * kPadRatio;

    // ---- 見出し + 百合紋の分割線 ----
    const std::wstring title = L"一時停止";
    const float titleScale = 0.9f * k;
    const Vector2 ts = text.Measure(title, titleScale);
    const float titleH = m_Short * kTitleRatio;
    const float titleY = m_PanelPos.y + pad + (titleH - ts.y) * 0.5f - titleH * 0.2f;
    text.Draw(title, { m_PanelPos.x + (m_PanelSize.x - ts.x) * 0.5f, titleY },
        { 0.96f, 0.92f, 0.84f, 1.0f }, titleScale);
    UIDeco::DrawDivider(sprite, true, { m_PanelPos.x + m_PanelSize.x * 0.5f, titleY + ts.y + titleH * 0.08f },
        m_PanelSize.x * 0.62f, gold);

    // ---- 項目 ----
    m_List.Draw(sprite, text, white, 0.55f * k);

    // ---- 操作の案内 ----
    const std::wstring hint = L"Esc / パッド Back で再開";
    const float hintScale = 0.36f * k;
    const Vector2 hs = text.Measure(hint, hintScale);
    const float hintTop = m_PanelPos.y + pad + titleH + m_List.Height();
    const float hintH = m_PanelPos.y + m_PanelSize.y - hintTop;
    text.Draw(hint, { m_PanelPos.x + (m_PanelSize.x - hs.x) * 0.5f, hintTop + (hintH - hs.y) * 0.5f },
        { 0.6f, 0.6f, 0.65f, 1.0f }, hintScale);
}
