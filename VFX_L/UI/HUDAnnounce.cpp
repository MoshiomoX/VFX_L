// ============================================================
// HUDAnnounce.cpp
// 画面の真ん中の案内（2026-10-07、難度曲線の見直しで足した「湧きの波」の予告など）。
//   文と出入りのフェードは StageDirector が決め、シーン → GameUI → HUDFrameInfo で受け取る。
//   出た瞬間は少し大きく（announcePunch）、0.25 秒で元の大きさへ。草の上でも読めるよう下敷きを敷く
// ============================================================
#include "UI/HUD.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "UI/UIDeco.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

void HUD::DrawAnnounce(SpriteRenderer& sprite, TextRenderer& text, const HUDFrameInfo& info)
{
    if (!info.announce || !*info.announce || info.announceAlpha <= 0.0f) return;
    const float a = (info.announceAlpha > 1.0f) ? 1.0f : info.announceAlpha;
    const float pop = 1.0f - info.announceAge / 0.25f;
    const float scale = m_S.announceScale * (1.0f + m_S.announcePunch * (pop > 0.0f ? pop : 0.0f));

    const Vector2 size = text.Measure(info.announce, scale);
    const Vector2 pos = { (m_ScreenW - size.x) * 0.5f, m_ScreenH * m_S.announceY - size.y * 0.5f };

    if (m_S.textPlates)
    {
        Vector4 plate = m_S.plateColor;
        plate.w *= a;
        UIDeco::DrawTextPlate(sprite, pos, size, m_S.platePad * 1.6f, plate, m_S.plateLineAlpha * a);
    }
    if (m_S.textShadow)
    {
        Vector4 sh = m_S.shadowColor;
        sh.w *= a;
        const float o = m_S.textShadowOffset;
        text.Draw(info.announce, { pos.x + o, pos.y + o }, sh, scale);
    }
    Vector4 col = m_S.announceColor;
    col.w *= a;
    text.Draw(info.announce, pos, col, scale);
}

void HUD::DrawAnnounceImGui()
{
    ImGui::Checkbox("Show Announce (center)", &m_Style.showAnnounce);
    ImGui::DragFloat("Announce Y (screen)", &m_Style.announceY, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Announce Scale", &m_Style.announceScale, 0.01f, 0.05f, 3.0f);
    ImGui::DragFloat("Announce Punch", &m_Style.announcePunch, 0.01f, 0.0f, 2.0f);
    ImGui::ColorEdit4("Announce Color", &m_Style.announceColor.x);
}
