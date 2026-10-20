// ============================================================
// HUDFps.cpp
// HUD の FPS 表示（2026-10-20 ユーザー「ImGui の無い環境でも fps を画面に出したい」）。
//   ミニマップの真下に「FPS 143  7.0 ms」を一行、文字の下敷き付き。値は EngineTimer の 1 秒平均（GameUI::Render が HUDFrameInfo::fps に入れる）。
//   出すかどうか = HUDStyle::showFps（ImGui / HUD.json）&& GameSettings::showFps（一時停止メニューの「設定」。Demo でも切れる）。
//   fpsWarnBelow より低い時だけ赤くする。HUD.cpp が長いので別ファイル
// ============================================================
#include "UI/HUD.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Core/GameSettings.h"

using namespace DirectX::SimpleMath;

void HUD::DrawFps(SpriteRenderer& sprite, TextRenderer& text, float fps)
{
    if (!m_S.showFps || !GameSettings::Get().showFps) return;
    if (fps <= 0.0f) return;   // 起動直後の 1 秒はまだ値が無い

    wchar_t buf[64];
    swprintf_s(buf, L"FPS %.0f  %.1f ms", fps, 1000.0f / fps);
    const std::wstring str = buf;

    const float scale = m_S.fpsScale;
    const Vector2 size = text.Measure(str, scale);
    const Vector2 anchor = m_S.fpsText.Resolve(m_ScreenW, m_ScreenH);
    const Vector2 pos = { anchor.x - size.x * 0.5f, anchor.y };   // x は中心、y は上端

    Plate(sprite, pos, size);

    const bool warn = (m_S.fpsWarnBelow > 0.0f && fps < m_S.fpsWarnBelow);
    if (m_S.textShadow)
    {
        const float o = m_S.textShadowOffset;
        text.Draw(str, { pos.x + o, pos.y + o }, m_S.shadowColor, scale);
    }
    text.Draw(str, pos, warn ? m_S.fpsWarnColor : m_S.textColor, scale);
}
