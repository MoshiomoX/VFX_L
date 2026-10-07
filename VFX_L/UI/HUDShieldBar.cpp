// ============================================================
// HUDShieldBar.cpp
// HUD のシールドのバー（2026-10-07。HUD.cpp が 1100 行を超えているので分けた）。
//   HP バーのすぐ下に細いバー、その右に絵 + 「今 / 上限」。
//   満タン = 明るい水色、減っている（戻り待ち・戻り中）= 暗い青（Megabonk の配色）。
//   減った分は HP と同じ残像で見せ、満タンに戻った瞬間に一度光らせる
// ============================================================
#include "UI/HUD.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Player/ShieldComponent.h"
#include "UI/UIDeco.h"

using namespace DirectX::SimpleMath;

namespace
{
    float Clamp01(float v) { return (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v; }
    float SafeRatio(float current, float max) { return (max > 0.0f) ? Clamp01(current / max) : 0.0f; }
}

// 残像と「満タンに戻った」光。モーダル表示中も進める（HUD::Update から）
void HUD::UpdateShield(float dt, const ShieldComponent* shield)
{
    if (!shield || shield->max <= 0.0f)
    {
        m_ShieldTrail.value = 1.0f;
        m_ShieldWasFull = true;
        m_ShieldFullFlash = 0.0f;
        return;
    }
    m_ShieldTrail.Update(dt, SafeRatio(shield->current, shield->max), m_Style);
    const bool full = shield->Full();
    if (full && !m_ShieldWasFull) m_ShieldFullFlash = 1.0f;
    m_ShieldWasFull = full;
    m_ShieldFullFlash = (std::max)(0.0f, m_ShieldFullFlash - dt / 0.5f);
}

float HUD::DrawShieldBar(SpriteRenderer& sprite, TextRenderer& text, const ShieldComponent& shield,
    const Vector2& hpPos)
{
    if (shield.max <= 0.0f) return 0.0f;

    const Vector2 pos = { hpPos.x, hpPos.y + m_S.hpBarSize.y + m_S.shieldBarGap };
    const Vector2 size = { m_S.hpBarSize.x, m_S.shieldBarHeight };

    Vector4 col = shield.Full() ? m_S.shieldColor : m_S.shieldChargingColor;
    if (m_ShieldFullFlash > 0.0f)
    {
        const float k = 1.0f + 1.5f * m_ShieldFullFlash;   // 1 を超えた分は bloom で滲む
        col.x *= k; col.y *= k; col.z *= k;
    }
    DrawBar(sprite, pos, size, SafeRatio(shield.current, shield.max), m_ShieldTrail.value, col, false);

    // ---- 右に絵 + 数字（草の上でも読めるよう下敷きを敷く）----
    wchar_t buf[32];
    swprintf_s(buf, L"%d/%d", (int)std::ceil(shield.current), (int)std::round(shield.max));
    const float k = UIDeco::UIScale();
    const float is = m_S.shieldIconSize;
    const Vector2 ts = text.Measure(buf, m_S.shieldTextScale);
    const float h = (std::max)(is, ts.y);
    const float x0 = pos.x + size.x + m_S.gemSize * 0.5f + 8.0f * k;
    const float cy = pos.y + size.y * 0.5f;
    const float iconGap = (is > 0.0f) ? 4.0f * k : 0.0f;
    Plate(sprite, { x0, cy - h * 0.5f }, { is + iconGap + ts.x, h });

    Vector4 tc = m_S.shieldTextColor;
    if (!shield.Full()) { tc.x *= 0.6f; tc.y *= 0.6f; tc.z *= 0.6f; }
    if (m_ShieldIcon && is > 0.0f)
    {
        const float o = m_S.textShadowOffset + 0.5f;
        sprite.Draw(m_ShieldIcon, { x0 + o, cy - is * 0.5f + o }, { is, is }, m_S.shadowColor);
        sprite.Draw(m_ShieldIcon, { x0, cy - is * 0.5f }, { is, is }, tc);
    }
    const Vector2 tp = { x0 + is + iconGap, cy - ts.y * 0.5f };
    if (m_S.textShadow)
    {
        const float o = m_S.textShadowOffset;
        text.Draw(buf, { tp.x + o, tp.y + o }, m_S.shadowColor, m_S.shieldTextScale);
    }
    text.Draw(buf, tp, tc, m_S.shieldTextScale);

    return m_S.shieldBarGap + m_S.shieldBarHeight;
}
