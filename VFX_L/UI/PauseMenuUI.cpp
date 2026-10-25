// ============================================================
// PauseMenuUI.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "UI/PauseMenuUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Manager/InputManager.h"
#include "Audio/AudioSystem.h"
#include "UI/UIDeco.h"
#include "UI/InputGlyph.h"

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
    BuildItems();
}

// 「設定」= 音量と敵味方の縁取りの ON / OFF（2026-10-07 ユーザー要望、10-08 から縁取りだけ）。SettingsMenuUI のページを重ねる。
// 「遊び方」= HowToPlayUI（2026-10-13）。チュートリアル中だけ最後に「チュートリアルを飛ばす」
void PauseMenuUI::BuildItems()
{
    std::vector<std::wstring> items = { L"再開する", L"設定", L"遊び方", L"最初からやり直す", L"タイトルへ戻る" };
    if (m_TutorialActive) items.push_back(L"チュートリアルを飛ばす");
    m_List.SetItems(std::move(items));
}

void PauseMenuUI::SetTutorialActive(bool on)
{
    if (on == m_TutorialActive) return;
    m_TutorialActive = on;
    BuildItems();
    Layout(m_Screen.x, m_Screen.y);
}

void PauseMenuUI::CloseSubPage()
{
    if (m_HowTo.IsOpen())
    {
        m_HowTo.Close();
        AudioSystem::Get().Play("ui_close");
        return;
    }
    if (!m_InSettings) return;
    m_InSettings = false;
    m_Settings.Close();   // 変えた物があれば保存
    AudioSystem::Get().Play("ui_close");
}

void PauseMenuUI::Layout(float screenW, float screenH)
{
    m_Screen = { screenW, screenH };
    m_Short = (std::min)(screenW, screenH);

    // UI 全体の倍率（2026-10-07）。箱の高さが画面短辺の 95% を超えるなら収まる所まで
    const float n = (float)m_List.Count();
    const float baseH = kPadRatio * 2.0f + kTitleRatio + itemHeightRatio * n + itemGapRatio * (n - 1.0f) + kHintRatio;
    m_Scale = UIDeco::FitScale(baseH, 0.95f);
    const float s = m_Short * m_Scale;

    const float pad = s * kPadRatio;
    const float itemH = s * itemHeightRatio;
    const float gap = s * itemGapRatio;
    const float listH = itemH * (float)m_List.Count() + gap * (float)(m_List.Count() - 1);

    m_PanelSize = { s * panelWidthRatio,
        pad + s * kTitleRatio + listH + s * kHintRatio + pad };
    m_PanelPos = { (screenW - m_PanelSize.x) * 0.5f, (screenH - m_PanelSize.y) * 0.5f };

    m_List.Layout({ m_PanelPos.x + pad, m_PanelPos.y + pad + s * kTitleRatio },
        { m_PanelSize.x - pad * 2.0f, itemH }, gap);
    m_List.accentColor = accentColor;
    m_Settings.Layout(screenW, screenH);
    m_HowTo.Layout(screenW, screenH);
}

PauseMenuUI::Action PauseMenuUI::HandleInput(float dt)
{
    // 設定 / 遊び方のページ：戻るまでそちらだけ（B / Esc もページを閉じるだけで、メニューは残る）
    if (m_InSettings)
    {
        if (m_Settings.HandleInput(dt)) m_InSettings = false;
        return Action::None;
    }
    if (m_HowTo.IsOpen())
    {
        m_HowTo.HandleInput(dt);
        return Action::None;
    }

    // パッドの B は「戻る」の決まりごとなので、カーソルに関係なく再開
    if (InputManager::Get().GetPadTrigger(XINPUT_GAMEPAD_B))
        return Action::Resume;

    switch (m_List.HandleInput(dt))
    {
    case 0:  return Action::Resume;
    case 1:
        m_InSettings = true;
        m_Settings.Open();
        return Action::None;
    case 2:
        m_HowTo.Open();
        AudioSystem::Get().Play("ui_open");
        return Action::None;
    case 3:  return Action::Restart;
    case 4:  return Action::Title;
    case 5:  return Action::SkipTutorial;
    default: return Action::None;
    }
}

// ============================================================
// 描画：暗幕 → 箱 → 見出し → 項目 → 操作の案内
// ============================================================
void PauseMenuUI::Draw(SpriteRenderer& sprite, TextRenderer& text, const std::shared_ptr<Texture>& white)
{
    if (!white) return;

    // 設定 / 遊び方のページを開いている間はそちらだけ描く（下のメニューは隠す）
    if (m_InSettings)
    {
        m_Settings.Draw(sprite, text, white);
        return;
    }
    if (m_HowTo.IsOpen())
    {
        m_HowTo.Draw(sprite, text, white);
        return;
    }

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

    const float k = m_Short / 900.0f * m_Scale;
    const float pad = m_Short * m_Scale * kPadRatio;

    // ---- 見出し + 百合紋の分割線 ----
    const std::wstring title = L"一時停止";
    const float titleScale = 0.9f * k;
    const Vector2 ts = text.Measure(title, titleScale);
    const float titleH = m_Short * m_Scale * kTitleRatio;
    const float titleY = m_PanelPos.y + pad + (titleH - ts.y) * 0.5f - titleH * 0.2f;
    text.Draw(title, { m_PanelPos.x + (m_PanelSize.x - ts.x) * 0.5f, titleY },
        { 0.96f, 0.92f, 0.84f, 1.0f }, titleScale);
    UIDeco::DrawDivider(sprite, true, { m_PanelPos.x + m_PanelSize.x * 0.5f, titleY + ts.y + titleH * 0.08f },
        m_PanelSize.x * 0.62f, gold);

    // ---- 項目 ----
    m_List.Draw(sprite, text, white, 0.55f * k);

    // ---- 操作の案内 ----
    const std::wstring hint = L"<{K:Esc}|{BACK} / {B}> で再開";   // <キーボード|パッド> の絵（InputGlyph）
    const float hintScale = 0.36f * k;
    const Vector2 hs = InputGlyph::Measure(text, hint, hintScale);
    const float hintTop = m_PanelPos.y + pad + titleH + m_List.Height();
    const float hintH = m_PanelPos.y + m_PanelSize.y - hintTop;
    InputGlyph::Draw(sprite, text, hint, { m_PanelPos.x + (m_PanelSize.x - hs.x) * 0.5f, hintTop + (hintH - hs.y) * 0.5f },
        { 0.6f, 0.6f, 0.65f, 1.0f }, hintScale);
}
