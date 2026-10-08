// ============================================================
// SettingsMenuUI.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "UI/SettingsMenuUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Manager/InputManager.h"
#include "Audio/AudioSystem.h"
#include "Core/GameSettings.h"
#include "UI/UIDeco.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kPadRatio = 0.04f;     // 余白（画面短辺に対する比率）
    constexpr float kTitleRatio = 0.10f;   // 見出しの高さ
    constexpr float kHintRatio = 0.07f;    // 案内の高さ
    constexpr float kControlRatio = 0.52f; // 行の幅のうち右側の操作部が占める割合
    constexpr float kVolumeStep = 0.05f;   // 1 段 = 5%
    constexpr float kRepeatFirst = 0.30f;  // 押しっぱなしで動き出すまで
    constexpr float kRepeatNext = 0.06f;   // それ以降の間隔
    constexpr float kFrame = 0.016f;       // 止まっている間も使うので固定の 1 フレーム

    const Vector4 kTextSel = { 0.96f, 0.92f, 0.84f, 1.0f };
    const Vector4 kTextDim = { 0.50f, 0.46f, 0.40f, 1.0f };
}

SettingsMenuUI::SettingsMenuUI()
{
    m_Rows = {
        { L"主音量",       Kind::Volume, (int)AudioSystem::Bus::Master },
        { L"効果音",       Kind::Volume, (int)AudioSystem::Bus::Sfx },
        { L"音楽",         Kind::Volume, (int)AudioSystem::Bus::Music },
        { L"UI の音",      Kind::Volume, (int)AudioSystem::Bus::Ui },
        { L"敵味方の縁取り", Kind::Toggle, 0 },   // 陣営の線だけ（トゥーンの黒い線は消えない。2026-10-08）
        { L"戻る",         Kind::Back,   0 },
    };
}

void SettingsMenuUI::Layout(float screenW, float screenH)
{
    m_Screen = { screenW, screenH };
    m_Short = (std::min)(screenW, screenH);

    // UI 全体の倍率（2026-10-07）。箱の高さが画面短辺の 95% を超えるなら収まる所まで
    const int n = (int)m_Rows.size();
    const float baseH = kPadRatio * 2.0f + kTitleRatio + rowHeightRatio * (float)n + rowGapRatio * (float)(n - 1) + kHintRatio;
    m_Scale = UIDeco::FitScale(baseH, 0.95f);
    const float s = m_Short * m_Scale;

    const float pad = s * kPadRatio;
    m_RowH = s * rowHeightRatio;
    m_Gap = s * rowGapRatio;
    const float listH = m_RowH * (float)n + m_Gap * (float)(n - 1);

    m_PanelSize = { s * panelWidthRatio,
        pad + s * kTitleRatio + listH + s * kHintRatio + pad };
    m_PanelPos = { (screenW - m_PanelSize.x) * 0.5f, (screenH - m_PanelSize.y) * 0.5f };
    m_RowsTop = m_PanelPos.y + pad + s * kTitleRatio;
}

void SettingsMenuUI::Open()
{
    m_Cursor = 0;
    m_InputDelay = 0.15f;
    m_RepeatDelay = 0.0f;
    m_Dragging = false;
    m_Dirty = false;
    m_LastMouse = { -1.0f, -1.0f };
}

void SettingsMenuUI::Close()
{
    if (!m_Dirty) return;
    AudioSystem::Get().SaveSettings();
    GameSettings::Get().Save();
    m_Dirty = false;
}

Vector2 SettingsMenuUI::RowPos(int i) const
{
    const float pad = m_Short * m_Scale * kPadRatio;
    return { m_PanelPos.x + pad, m_RowsTop + (m_RowH + m_Gap) * (float)i };
}

void SettingsMenuUI::ControlRect(int i, Vector2& pos, Vector2& size) const
{
    const float pad = m_Short * m_Scale * kPadRatio;
    const float rowW = m_PanelSize.x - pad * 2.0f;
    const Vector2 rp = RowPos(i);
    size = { rowW * kControlRatio, m_RowH };
    pos = { rp.x + rowW - size.x, rp.y };
}

float SettingsMenuUI::GetValue(const Row& r) const
{
    switch (r.kind)
    {
    case Kind::Volume: return AudioSystem::Get().GetVolume((AudioSystem::Bus)r.bus);
    case Kind::Toggle: return GameSettings::Get().factionOutline ? 1.0f : 0.0f;
    default:           return 0.0f;
    }
}

void SettingsMenuUI::SetValue(const Row& r, float v)
{
    switch (r.kind)
    {
    case Kind::Volume:
    {
        v = std::clamp(v, 0.0f, 1.0f);
        if (std::fabs(v - GetValue(r)) < 1e-4f) return;
        AudioSystem::Get().SetVolume((AudioSystem::Bus)r.bus, v);
        m_Dirty = true;
        break;
    }
    case Kind::Toggle:
    {
        const bool on = v > 0.5f;
        if (GameSettings::Get().factionOutline == on) return;
        GameSettings::Get().factionOutline = on;
        m_Dirty = true;
        break;
    }
    default: break;
    }
}

// 左右：音量は 5% ずつ（5% の目盛へ丸める）、切替は反転
void SettingsMenuUI::Step(int dir)
{
    const Row& r = m_Rows[m_Cursor];
    if (r.kind == Kind::Volume)
    {
        const float cur = GetValue(r);
        const float snapped = std::round(cur / kVolumeStep) * kVolumeStep;
        SetValue(r, snapped + kVolumeStep * (float)dir);
        AudioSystem::Get().Play("ui_hover");
    }
    else if (r.kind == Kind::Toggle)
    {
        SetValue(r, GetValue(r) > 0.5f ? 0.0f : 1.0f);
        AudioSystem::Get().Play("ui_select");
    }
}

void SettingsMenuUI::Activate()
{
    const Row& r = m_Rows[m_Cursor];
    if (r.kind == Kind::Toggle) Step(+1);
}

// ============================================================
// 入力。戻るなら true
// ============================================================
bool SettingsMenuUI::HandleInput()
{
    auto& input = InputManager::Get();
    const int n = (int)m_Rows.size();

    // B / Esc は常に戻る（Esc は GameUI が先に見て CloseSettings を呼ぶが、ここでも受ける）
    if (input.GetPadTrigger(XINPUT_GAMEPAD_B) || input.GetKeyTrigger(VK_ESCAPE))
    {
        Close();
        AudioSystem::Get().Play("ui_close");
        return true;
    }

    const auto mp = input.GetMousePos();
    const Vector2 mouse = { mp.x, mp.y };
    const bool mouseMoved = (mouse - m_LastMouse).LengthSquared() > 0.25f;
    m_LastMouse = mouse;

    if (m_InputDelay > 0.0f)
    {
        m_InputDelay -= kFrame;
        return false;
    }

    // ---- マウス：動かした時だけ行を選ぶ ----
    const float pad = m_Short * m_Scale * kPadRatio;
    const float rowW = m_PanelSize.x - pad * 2.0f;
    int hover = -1;
    for (int i = 0; i < n; ++i)
    {
        const Vector2 p = RowPos(i);
        if (mouse.x >= p.x && mouse.x <= p.x + rowW && mouse.y >= p.y && mouse.y <= p.y + m_RowH)
        {
            hover = i;
            break;
        }
    }
    if (hover >= 0 && mouseMoved && !m_Dragging)
    {
        if (hover != m_Cursor) AudioSystem::Get().Play("ui_hover");
        m_Cursor = hover;
    }
    m_Cursor = std::clamp(m_Cursor, 0, n - 1);

    // ---- 上下 ----
    const int before = m_Cursor;
    if (input.GetKeyTrigger(VK_UP) || input.GetKeyTrigger('W') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_UP))
        m_Cursor = (m_Cursor + n - 1) % n;
    if (input.GetKeyTrigger(VK_DOWN) || input.GetKeyTrigger('S') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_DOWN))
        m_Cursor = (m_Cursor + 1) % n;
    if (m_Cursor != before) AudioSystem::Get().Play("ui_hover");

    // ---- 左右（押しっぱなしで連続）----
    const bool leftHeld = input.GetKeyPress(VK_LEFT) || input.GetKeyPress('A') || input.GetPadPress(XINPUT_GAMEPAD_DPAD_LEFT);
    const bool rightHeld = input.GetKeyPress(VK_RIGHT) || input.GetKeyPress('D') || input.GetPadPress(XINPUT_GAMEPAD_DPAD_RIGHT);
    const bool leftTrig = input.GetKeyTrigger(VK_LEFT) || input.GetKeyTrigger('A') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_LEFT);
    const bool rightTrig = input.GetKeyTrigger(VK_RIGHT) || input.GetKeyTrigger('D') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_RIGHT);
    if (leftTrig || rightTrig)
    {
        Step(rightTrig ? +1 : -1);
        m_RepeatDelay = kRepeatFirst;
    }
    else if ((leftHeld || rightHeld) && m_Rows[m_Cursor].kind == Kind::Volume)
    {
        m_RepeatDelay -= kFrame;
        if (m_RepeatDelay <= 0.0f)
        {
            Step(rightHeld ? +1 : -1);
            m_RepeatDelay = kRepeatNext;
        }
    }

    // ---- 決定 ----
    if (input.GetKeyTrigger(VK_RETURN) || input.GetKeyTrigger(VK_SPACE) || input.GetPadTrigger(XINPUT_GAMEPAD_A))
    {
        if (m_Rows[m_Cursor].kind == Kind::Back)
        {
            Close();
            AudioSystem::Get().Play("ui_select");
            return true;
        }
        Activate();
    }

    // ---- マウスのクリック / ドラッグ ----
    if (input.GetMouseTrigger(0) && hover >= 0)
    {
        m_Cursor = hover;
        const Row& r = m_Rows[hover];
        Vector2 cp, cs;
        ControlRect(hover, cp, cs);
        const float arrowW = cs.y;   // 両端の ◀ ▶ は行の高さと同じ幅
        if (r.kind == Kind::Back)
        {
            Close();
            AudioSystem::Get().Play("ui_select");
            return true;
        }
        if (mouse.x >= cp.x && mouse.x <= cp.x + cs.x)
        {
            if (mouse.x < cp.x + arrowW) Step(-1);
            else if (mouse.x > cp.x + cs.x - arrowW) Step(+1);
            else if (r.kind == Kind::Volume) m_Dragging = true;
            else Step(+1);
        }
        else if (r.kind == Kind::Toggle)
            Step(+1);   // 行のどこを押しても切替
    }
    if (m_Dragging)
    {
        if (!input.GetMousePress(0)) m_Dragging = false;
        else if (m_Rows[m_Cursor].kind == Kind::Volume)
        {
            Vector2 cp, cs;
            ControlRect(m_Cursor, cp, cs);
            const float arrowW = cs.y;
            const float trackX = cp.x + arrowW;
            const float trackW = cs.x - arrowW * 2.0f;
            const float t = std::clamp((mouse.x - trackX) / (std::max)(1.0f, trackW), 0.0f, 1.0f);
            SetValue(m_Rows[m_Cursor], std::round(t / kVolumeStep) * kVolumeStep);
        }
    }
    return false;
}

// ============================================================
// 描画：暗幕 → 箱 → 見出し → 行（左に名前、右に ◀ 操作部 ▶）→ 案内
// ============================================================
void SettingsMenuUI::Draw(SpriteRenderer& sprite, TextRenderer& text, const std::shared_ptr<Texture>& white)
{
    if (!white) return;

    sprite.Draw(white, { 0.0f, 0.0f }, m_Screen, dimColor);

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
    const float rowW = m_PanelSize.x - pad * 2.0f;

    // ---- 見出し ----
    const std::wstring title = L"設定";
    const float titleScale = 0.9f * k;
    const Vector2 ts = text.Measure(title, titleScale);
    const float titleH = m_Short * m_Scale * kTitleRatio;
    const float titleY = m_PanelPos.y + pad + (titleH - ts.y) * 0.5f - titleH * 0.2f;
    text.Draw(title, { m_PanelPos.x + (m_PanelSize.x - ts.x) * 0.5f, titleY }, kTextSel, titleScale);
    UIDeco::DrawDivider(sprite, true, { m_PanelPos.x + m_PanelSize.x * 0.5f, titleY + ts.y + titleH * 0.08f },
        m_PanelSize.x * 0.62f, gold);

    // ---- 行 ----
    const float labelScale = 0.5f * k;
    const float valueScale = 0.42f * k;
    for (int i = 0; i < (int)m_Rows.size(); ++i)
    {
        const Row& r = m_Rows[i];
        const bool sel = (i == m_Cursor);
        const Vector2 p = RowPos(i);
        const Vector2 size = { rowW, m_RowH };

        UIDeco::PanelStyle ps;
        ps.fill = sel ? Vector4(0.010f, 0.008f, 0.016f, 0.96f) : Vector4(0.003f, 0.0025f, 0.005f, 0.90f);
        ps.innerInset = 3.0f;
        ps.innerAlpha = sel ? 0.45f : 0.20f;
        Vector4 line = gold;
        if (!sel) line.w = 0.55f;
        UIDeco::DrawPanel(sprite, p, size, line, ps, sel ? 1.0f : 0.0f);

        const Vector4 textCol = sel ? kTextSel : kTextDim;
        const Vector2 ls = text.Measure(r.label, labelScale);
        const float cy = p.y + m_RowH * 0.5f;

        if (r.kind == Kind::Back)
        {
            // 「戻る」は中央にラベルだけ（一時停止の項目と同じ見た目）
            text.Draw(r.label, { p.x + (rowW - ls.x) * 0.5f, cy - ls.y * 0.5f }, textCol, labelScale);
            if (sel)
            {
                const float g = m_RowH * 0.22f;
                for (const float cx : { p.x, p.x + rowW })
                    sprite.Draw(white, { cx - g * 0.5f, cy - g * 0.5f }, { g, g }, gold, 0.785398f, { cx, cy });
            }
            continue;
        }

        text.Draw(r.label, { p.x + m_RowH * 0.35f, cy - ls.y * 0.5f }, textCol, labelScale);

        // ---- 操作部：◀ [スライダー / オン・オフ] ▶ ----
        Vector2 cp, cs;
        ControlRect(i, cp, cs);
        const float arrowW = cs.y;
        // 両端の矢印は文字で（三角の絵は無い）
        const Vector4 arrowCol = sel ? gold : Vector4(gold.x, gold.y, gold.z, 0.45f);
        auto drawArrow = [&](float cx, const wchar_t* s)
        {
            const Vector2 as = text.Measure(s, labelScale);
            text.Draw(s, { cx - as.x * 0.5f, cy - as.y * 0.5f }, arrowCol, labelScale);
        };
        drawArrow(cp.x + arrowW * 0.5f, L"<");
        drawArrow(cp.x + cs.x - arrowW * 0.5f, L">");

        const float trackX = cp.x + arrowW;
        const float trackW = cs.x - arrowW * 2.0f;
        if (r.kind == Kind::Volume)
        {
            const float v = GetValue(r);
            const float th = (std::max)(2.0f, m_RowH * 0.06f);
            const float knob = m_RowH * 0.26f;
            const float innerX = trackX + knob * 0.5f;
            const float innerW = trackW - knob;
            // 地の線 → 塗った分 → つまみ（菱形）→ 数字
            sprite.Draw(white, { innerX, cy - th * 0.5f }, { innerW, th }, Vector4(gold.x, gold.y, gold.z, sel ? 0.30f : 0.18f));
            sprite.Draw(white, { innerX, cy - th * 0.5f }, { innerW * v, th }, Vector4(gold.x, gold.y, gold.z, sel ? 0.95f : 0.55f));
            const float kx = innerX + innerW * v;
            sprite.Draw(white, { kx - knob * 0.5f, cy - knob * 0.5f }, { knob, knob }, sel ? gold : Vector4(gold.x, gold.y, gold.z, 0.6f), 0.785398f, { kx, cy });

            const std::wstring num = std::to_wstring((int)std::lround(v * 100.0f));
            const Vector2 ns = text.Measure(num, valueScale);
            // 数字はスライダーの左（ラベルと操作部の間）に右寄せ
            text.Draw(num, { cp.x - ns.x - m_RowH * 0.2f, cy - ns.y * 0.5f }, textCol, valueScale);
        }
        else
        {
            const bool on = GetValue(r) > 0.5f;
            const std::wstring label = on ? L"オン" : L"オフ";
            const Vector2 vs = text.Measure(label, labelScale);
            text.Draw(label, { trackX + (trackW - vs.x) * 0.5f, cy - vs.y * 0.5f },
                on ? textCol : Vector4(textCol.x * 0.75f, textCol.y * 0.75f, textCol.z * 0.75f, 1.0f), labelScale);
        }
    }

    // ---- 案内 ----
    const std::wstring hint = L"← → で変更　Enter で決定　Esc / パッド B で戻る";
    const float hintScale = 0.36f * k;
    const Vector2 hs = text.Measure(hint, hintScale);
    const int n = (int)m_Rows.size();
    const float hintTop = m_RowsTop + m_RowH * (float)n + m_Gap * (float)(n - 1);
    const float hintH = m_PanelPos.y + m_PanelSize.y - hintTop;
    text.Draw(hint, { m_PanelPos.x + (m_PanelSize.x - hs.x) * 0.5f, hintTop + (hintH - hs.y) * 0.5f },
        { 0.6f, 0.6f, 0.65f, 1.0f }, hintScale);
}
