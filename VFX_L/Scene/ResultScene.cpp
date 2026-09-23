// ============================================================
// ResultScene.cpp
// ============================================================
#include "Scene/ResultScene.h"
#include "Scene/RunResult.h"
#include "Core/Application.h"
#include "Graphics/Material/Texture.h"
#include "Manager/InputManager.h"
#include "ResourcePaths.h"
#include <cmath>
#include <cstdio>
#include <iostream>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kHeaderScale = 1.2f;
    constexpr float kRowScale = 0.6f;
    constexpr float kPromptScale = 0.45f;
    constexpr int   kRowCount = 4;
    constexpr float kRowInterval = 0.25f;   // 行が 1 つ出るまでの秒数
    constexpr float kRowFade = 0.3f;        // 行のフェード時間
    constexpr float kAllShown = kRowInterval * kRowCount + kRowFade;

    float Clamp01(float v)
    {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }
}

// ============================================================
// Init
// ============================================================
void ResultScene::Init()
{
    std::cout << "[ResultScene] Init" << std::endl;

    auto& gfx = Application::Get().GetGraphics();
    auto* device = gfx.GetDevice();
    auto* context = gfx.GetContext();
    m_ScreenW = gfx.GetWidth();
    m_ScreenH = gfx.GetHeight();

    // 背景のグリッド用。動かさない
    m_Camera.Init(45.0f, m_ScreenW / m_ScreenH, 0.1f, 10000.0f);
    m_Camera.LookAt({ 0.0f, 10.0f, -18.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    SetCamera(&m_Camera);

    if (!m_Sprite.Initialize(device, context, 64))
        std::cout << "[Error] ResultScene: SpriteRenderer init failed" << std::endl;
    m_Sprite.SetScreenSize(m_ScreenW, m_ScreenH);

    if (!m_Text.Initialize(device, context, Res::Fnt::JP))
        std::cout << "[Error] ResultScene: TextRenderer init failed" << std::endl;

    m_WhiteTex = std::make_shared<Texture>();
    if (!m_WhiteTex->CreateSolid(device, 255, 255, 255, 255))
        m_WhiteTex.reset();

    BuildRows();

    m_Time = 0.0f;
    m_Leaving = false;
    std::cout << "[ResultScene] Init complete" << std::endl;
}

// ============================================================
// 表示する行を組む。valid でなければ全部 "--"
// ============================================================
void ResultScene::BuildRows()
{
    const RunResult& r = g_LastRun;
    wchar_t buf[64];

    m_Rows[0].label = L"Survived";
    if (r.valid)
    {
        const int total = (int)r.survivedSec;
        swprintf_s(buf, L"%02d:%02d", total / 60, total % 60);
        m_Rows[0].value = buf;
    }
    else m_Rows[0].value = L"--:--";

    m_Rows[1].label = L"Level";
    if (r.valid) { swprintf_s(buf, L"%d", r.level); m_Rows[1].value = buf; }
    else m_Rows[1].value = L"--";

    m_Rows[2].label = L"Kills";
    if (r.valid) { swprintf_s(buf, L"%u", r.kills); m_Rows[2].value = buf; }
    else m_Rows[2].value = L"--";

    m_Rows[3].label = L"Exp Gained";
    if (r.valid) { swprintf_s(buf, L"%d", (int)r.expGained); m_Rows[3].value = buf; }
    else m_Rows[3].value = L"--";
}

// ============================================================
// Shutdown
// ============================================================
void ResultScene::Shutdown()
{
    m_Text.Shutdown();
    m_Sprite.Shutdown();
    std::cout << "[ResultScene] Shutdown" << std::endl;
}

// ============================================================
// Update
// ============================================================
void ResultScene::Update(float dt)
{
    SceneBase::Update(dt);
    m_Time += dt;

    auto& gfx = Application::Get().GetGraphics();
    if (gfx.GetWidth() != m_ScreenW || gfx.GetHeight() != m_ScreenH)
    {
        m_ScreenW = gfx.GetWidth();
        m_ScreenH = gfx.GetHeight();
        m_Sprite.SetScreenSize(m_ScreenW, m_ScreenH);
        m_Camera.Init(45.0f, m_ScreenW / m_ScreenH, 0.1f, 10000.0f);
    }

    if (m_Leaving) return;

    // 行が全部出るまでは入力を受けない（連打で読み飛ばすのを防ぐ）
    if (m_Time < kAllShown) return;

    auto& input = InputManager::Get();
    auto& sm = Application::Get().GetGame().GetSceneManager();

    const bool retry = input.GetKeyTrigger(VK_RETURN)
        || input.GetKeyTrigger(VK_SPACE)
        || input.GetPadTrigger(XINPUT_GAMEPAD_A);
    // ※Esc はアプリ終了（Window.cpp）に取られているので使わない
    const bool toTitle = input.GetKeyTrigger(VK_BACK)
        || input.GetPadTrigger(XINPUT_GAMEPAD_B);

    if (retry)
    {
        m_Leaving = true;
        sm.RequestChangeScene(SceneType::COLLISION_TEST);
    }
    else if (toTitle)
    {
        m_Leaving = true;
        sm.RequestChangeScene(SceneType::TITLE);
    }
}

// ============================================================
// Render
// ============================================================
void ResultScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);

    m_Sprite.Begin();
    m_Text.Begin();

    if (m_WhiteTex)
        m_Sprite.Draw(m_WhiteTex, { 0.0f, 0.0f }, { m_ScreenW, m_ScreenH }, { 0.02f, 0.02f, 0.05f, 0.8f });

    // ---- 見出し ----
    const std::wstring header = L"RESULT";
    const Vector2 headerSize = m_Text.Measure(header, kHeaderScale);
    const Vector2 headerPos = { (m_ScreenW - headerSize.x) * 0.5f, m_ScreenH * 0.14f };
    m_Text.Draw(header, headerPos + Vector2(3.0f, 3.0f), { 0.0f, 0.0f, 0.0f, 0.8f }, kHeaderScale);
    m_Text.Draw(header, headerPos, { 1.0f, 0.92f, 0.6f, 1.0f }, kHeaderScale);

    // ---- 行（ラベル左寄せ、値右寄せ。中央の帯に収める）----
    const float bandW = 520.0f;
    const float bandX = (m_ScreenW - bandW) * 0.5f;
    const float rowH = m_Text.GetLineHeight(kRowScale) + 18.0f;
    float y = m_ScreenH * 0.36f;

    for (int i = 0; i < kRowCount; ++i)
    {
        const float t = Clamp01((m_Time - kRowInterval * (float)i) / kRowFade);
        if (t <= 0.0f) break;

        // 出現中は少し右から滑り込む
        const float slide = (1.0f - t) * 30.0f;
        const Vector4 labelColor = { 0.8f, 0.8f, 0.8f, t };
        const Vector4 valueColor = { 1.0f, 1.0f, 1.0f, t };

        if (m_WhiteTex)
            m_Sprite.Draw(m_WhiteTex, { bandX, y + rowH - 6.0f }, { bandW, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.25f * t });

        m_Text.Draw(m_Rows[i].label, { bandX + slide, y }, labelColor, kRowScale);
        const Vector2 vs = m_Text.Measure(m_Rows[i].value, kRowScale);
        m_Text.Draw(m_Rows[i].value, { bandX + bandW - vs.x + slide, y }, valueColor, kRowScale);

        y += rowH;
    }

    // ---- 案内 ----
    if (m_Time >= kAllShown)
    {
        const float blink = 0.55f + 0.45f * std::sin(m_Time * 3.0f);
        const std::wstring prompt = m_Leaving ? L"Loading..." : L"Enter: Retry     BackSpace: Title";
        const Vector2 ps = m_Text.Measure(prompt, kPromptScale);
        m_Text.Draw(prompt, { (m_ScreenW - ps.x) * 0.5f, m_ScreenH * 0.8f },
            { 1.0f, 1.0f, 1.0f, m_Leaving ? 1.0f : blink }, kPromptScale);
    }

    m_Sprite.End();
    m_Text.End();
}
