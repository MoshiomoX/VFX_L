// ============================================================
// HUDEliteBars.cpp
// 面の固定エリートの HP 条（2026-10-25）。Boss の条の下、画面幅の 3 割で 1 体 1 本。
//   条の中に名前（「疾風・鉄壁の 大斧の戦鬼」）、鉄壁の盾が残っていれば水色の縁、狂暴なら明るい赤で脈打つ
// ============================================================
#include "UI/HUD.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "UI/UIDeco.h"

using namespace DirectX::SimpleMath;

void HUD::DrawEliteBars(SpriteRenderer& sprite, TextRenderer& text, const HUDFrameInfo& info, float centerX, float top)
{
    const float k = UIDeco::UIScale();
    const Vector2 size = { m_ScreenW * 0.3f, m_S.hpBarSize.y * 0.85f };
    float y = top + 6.0f * k;
    for (const HUDEliteBar& b : info.elites)
    {
        const Vector2 pos = { centerX - size.x * 0.5f, y };
        const float r = (b.hp < 0.0f) ? 0.0f : (b.hp > 1.0f) ? 1.0f : b.hp;
        Vector4 fill = { 0.55f, 0.06f, 0.05f, 1.0f };   // 暗い赤（線形）
        if (b.enraged)
        {
            const float p = 0.5f + 0.5f * std::sin(UIDeco::Clock() * 7.0f);
            fill = { 0.85f + 0.4f * p, 0.10f, 0.06f, 1.0f };
        }
        DrawBar(sprite, pos, size, r, r, fill, true);
        if (b.shield)
        {
            // 盾：条の外側に水色の細い縁（線形の色）
            const float t = 2.0f * k;
            const Vector4 c = { 0.25f, 0.65f, 1.0f, 1.0f };
            sprite.Draw(m_WhiteTex, { pos.x - t, pos.y - t }, { size.x + t * 2.0f, t }, c);
            sprite.Draw(m_WhiteTex, { pos.x - t, pos.y + size.y }, { size.x + t * 2.0f, t }, c);
            sprite.Draw(m_WhiteTex, { pos.x - t, pos.y }, { t, size.y }, c);
            sprite.Draw(m_WhiteTex, { pos.x + size.x, pos.y }, { t, size.y }, c);
        }
        DrawBarLabel(text, b.name, pos, size);
        y += size.y + 6.0f * k;
    }
}
