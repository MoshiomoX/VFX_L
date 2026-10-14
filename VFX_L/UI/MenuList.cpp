// ============================================================
// MenuList.cpp
// ============================================================
#include "UI/MenuList.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Manager/InputManager.h"
#include "UI/UIDeco.h"
#include "Audio/AudioSystem.h"

using namespace DirectX::SimpleMath;

void MenuList::Layout(const Vector2& topLeft, const Vector2& itemSize, float gap)
{
    m_TopLeft = topLeft;
    m_ItemSize = itemSize;
    m_Gap = gap;
}

float MenuList::Height() const
{
    const int n = Count();
    return (n > 0) ? m_ItemSize.y * (float)n + m_Gap * (float)(n - 1) : 0.0f;
}

Vector2 MenuList::ItemPos(int i) const
{
    return { m_TopLeft.x, m_TopLeft.y + (m_ItemSize.y + m_Gap) * (float)i };
}

void MenuList::Open()
{
    m_Cursor = 0;
    m_InputDelay = 0.15f;
    m_LastMouse = { -1.0f, -1.0f };
}

// ============================================================
// マウスは「動かした時だけ」カーソルを奪う（キーで選んでいる途中に、
// 止まっているマウスの位置へ戻されないように）
// ============================================================
// 選択の移動・決定の音（2026-10-03）。判定そのものは HandleInputRaw
int MenuList::HandleInput()
{
    const int before = m_Cursor;
    const int picked = HandleInputRaw();
    if (picked >= 0) AudioSystem::Get().Play("ui_select");
    else if (m_Cursor != before && before >= 0) AudioSystem::Get().Play("ui_hover");
    return picked;
}

int MenuList::HandleInputRaw()
{
    const int n = Count();
    if (n <= 0) return -1;

    auto& input = InputManager::Get();
    const auto mp = input.GetMousePos();
    const Vector2 mouse = { mp.x, mp.y };
    const bool mouseMoved = (mouse - m_LastMouse).LengthSquared() > 0.25f;
    m_LastMouse = mouse;

    if (m_InputDelay > 0.0f)
    {
        m_InputDelay -= 0.016f;
        return -1;
    }

    int hover = -1;
    for (int i = 0; i < n; ++i)
    {
        const Vector2 p = ItemPos(i);
        if (mouse.x >= p.x && mouse.x <= p.x + m_ItemSize.x &&
            mouse.y >= p.y && mouse.y <= p.y + m_ItemSize.y)
        {
            hover = i;
            break;
        }
    }
    if (hover >= 0 && mouseMoved) m_Cursor = hover;
    if (m_Cursor < 0 || m_Cursor >= n) m_Cursor = 0;

    if (input.GetKeyTrigger(VK_UP) || input.GetKeyTrigger('W') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_UP))
        m_Cursor = (m_Cursor + n - 1) % n;
    if (input.GetKeyTrigger(VK_DOWN) || input.GetKeyTrigger('S') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_DOWN))
        m_Cursor = (m_Cursor + 1) % n;

    if (input.GetMouseTrigger(0) && hover >= 0)
    {
        m_Cursor = hover;
        return m_Cursor;
    }
    if (input.GetKeyTrigger(VK_RETURN) || input.GetKeyTrigger(VK_SPACE) || input.GetPadTrigger(XINPUT_GAMEPAD_A))
        return m_Cursor;

    return -1;
}

// ============================================================
// 選択中は色付きの地と枠、他は薄い地だけ
// ============================================================
void MenuList::Draw(SpriteRenderer& sprite, TextRenderer& text,
    const std::shared_ptr<Texture>& white, float textScale) const
{
    if (!white) return;
    if (style == Style::Text)
    {
        DrawTextStyle(sprite, text, white, textScale);
        return;
    }

    // 幻想 UI：暗い地 + 古金の細い二重線。選択中は線が明るく光り、両端に菱形
    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    for (int i = 0; i < Count(); ++i)
    {
        const Vector2 p = ItemPos(i);
        const bool sel = (i == m_Cursor);

        UIDeco::PanelStyle ps;
        ps.fill = sel ? Vector4(0.010f, 0.008f, 0.016f, 0.96f) : Vector4(0.003f, 0.0025f, 0.005f, 0.90f);
        ps.innerInset = 3.0f;
        ps.innerAlpha = sel ? 0.45f : 0.20f;
        Vector4 line = gold;
        if (!sel) line.w = 0.55f;
        UIDeco::DrawPanel(sprite, p, m_ItemSize, line, ps, sel ? 1.0f : 0.0f);

        if (sel)
        {
            const float g = m_ItemSize.y * 0.22f;
            const float cy = p.y + m_ItemSize.y * 0.5f;
            for (const float cx : { p.x, p.x + m_ItemSize.x })
                sprite.Draw(white, { cx - g * 0.5f, cy - g * 0.5f }, { g, g }, gold, 0.785398f, { cx, cy });
        }

        const Vector2 ls = text.Measure(m_Labels[i], textScale);
        text.Draw(m_Labels[i], { p.x + (m_ItemSize.x - ls.x) * 0.5f, p.y + (m_ItemSize.y - ls.y) * 0.5f },
            sel ? Vector4(0.96f, 0.92f, 0.84f, 1.0f) : Vector4(0.50f, 0.46f, 0.40f, 1.0f), textScale);
    }
}

// ============================================================
// 文字だけの一覧（タイトル、2026-10-09）。背景の 3D の景色の上に直に置く。
// 選んでいる項目：左に古金の菱形、後ろに左から右へ消える金の光、文字の下に線。少し右へずれる。
// 文字は暗い縁取り（4 方向にずらした影）で、明るい空・草の上でも読めるように
// ============================================================
void MenuList::DrawTextStyle(SpriteRenderer& sprite, TextRenderer& text,
    const std::shared_ptr<Texture>& white, float textScale) const
{
    const int n = Count();
    if ((int)m_SelAnim.size() != n) m_SelAnim.assign(n, 0.0f);
    const float now = UIDeco::Clock();
    const float dt = (m_AnimClock < 0.0f) ? 0.0f : (std::min)(0.1f, now - m_AnimClock);
    m_AnimClock = now;

    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    for (int i = 0; i < n; ++i)
    {
        float& s = m_SelAnim[i];
        const float target = (i == m_Cursor) ? 1.0f : 0.0f;
        s += (target - s) * (std::min)(1.0f, dt * 12.0f);
        const float e = s * s * (3.0f - 2.0f * s);

        const Vector2 p = ItemPos(i);
        const float h = m_ItemSize.y;

        // 後ろの光（左が濃く、右へ消える。細い帯を並べて段々に）
        if (e > 0.01f)
        {
            constexpr int kStrips = 16;
            const float w = m_ItemSize.x;
            for (int k = 0; k < kStrips; ++k)
            {
                const float t = (float)k / (float)kStrips;
                const float a = 0.30f * e * (1.0f - t) * (1.0f - t) * fade;
                sprite.Draw(white, { p.x + w * t, p.y + h * 0.08f }, { w / (float)kStrips + 1.0f, h * 0.84f },
                    { gold.x, gold.y, gold.z, a });
            }
        }

        // 左の菱形
        {
            const float g = h * (0.16f + 0.08f * e);
            const Vector2 c = { p.x + h * 0.32f, p.y + h * 0.5f };
            Vector4 col = gold;
            col.w = (0.35f + 0.65f * e) * fade;
            sprite.Draw(white, { c.x - g * 0.5f, c.y - g * 0.5f }, { g, g }, col, 0.785398f, c);   // 回転の中心はピクセル
        }

        // 文字（暗い縁取り → 本体）
        const std::wstring& label = m_Labels[i];
        const Vector2 ls = text.Measure(label, textScale);
        const Vector2 tp = { p.x + h * 0.70f + h * 0.22f * e, p.y + (h - ls.y) * 0.5f };
        const float o = (std::max)(1.0f, h * 0.035f);
        const Vector4 shadow = { 0.0f, 0.0f, 0.0f, 0.75f * fade };
        for (const Vector2 d : { Vector2(o, 0.0f), Vector2(-o, 0.0f), Vector2(0.0f, o), Vector2(0.0f, -o), Vector2(o, o) })
            text.Draw(label, tp + d, shadow, textScale);
        const Vector4 dim = { 0.70f, 0.64f, 0.55f, 0.90f };
        const Vector4 lit = { 1.00f, 0.90f, 0.62f, 1.00f };
        Vector4 col = Vector4::Lerp(dim, lit, e);
        col.w *= fade;
        text.Draw(label, tp, col, textScale);

        // 下の線（選んだ時に左から伸びる）
        if (e > 0.01f)
        {
            Vector4 line = gold;
            line.w = 0.9f * e * fade;
            sprite.Draw(white, { tp.x, tp.y + ls.y + h * 0.02f }, { ls.x * e, (std::max)(1.0f, h * 0.03f) }, line);
        }
    }
}
