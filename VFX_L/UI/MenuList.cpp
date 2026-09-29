// ============================================================
// MenuList.cpp
// ============================================================
#include "UI/MenuList.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Manager/InputManager.h"
#include "UI/UIDeco.h"

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
int MenuList::HandleInput()
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
