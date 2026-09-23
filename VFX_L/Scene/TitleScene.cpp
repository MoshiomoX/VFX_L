// ============================================================
// TitleScene.cpp
// ============================================================
#include "Scene/TitleScene.h"
#include "Core/Application.h"
#include "Graphics/Material/Texture.h"
#include "Manager/InputManager.h"
#include "ResourcePaths.h"
#include <cmath>
#include <iostream>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kTitleScale = 1.6f;
    constexpr float kPromptScale = 0.55f;
    constexpr float kHintScale = 0.4f;
    constexpr float kOrbitRadius = 14.0f;
    constexpr float kOrbitSpeed = 0.15f;   // rad / 秒
}

// ============================================================
// Init
// ============================================================
void TitleScene::Init()
{
    std::cout << "[TitleScene] Init" << std::endl;

    auto& gfx = Application::Get().GetGraphics();
    auto* device = gfx.GetDevice();
    auto* context = gfx.GetContext();
    m_ScreenW = gfx.GetWidth();
    m_ScreenH = gfx.GetHeight();

    // 背景用のカメラ。グリッドを少し見下ろす
    m_Camera.Init(45.0f, m_ScreenW / m_ScreenH, 0.1f, 10000.0f);
    m_Camera.LookAt({ 0.0f, 6.0f, -kOrbitRadius }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    SetCamera(&m_Camera);

    if (!m_Sprite.Initialize(device, context, 64))
        std::cout << "[Error] TitleScene: SpriteRenderer init failed" << std::endl;
    m_Sprite.SetScreenSize(m_ScreenW, m_ScreenH);

    if (!m_Text.Initialize(device, context, Res::Fnt::JP))
        std::cout << "[Error] TitleScene: TextRenderer init failed" << std::endl;

    m_WhiteTex = std::make_shared<Texture>();
    if (!m_WhiteTex->CreateSolid(device, 255, 255, 255, 255))
        m_WhiteTex.reset();

    m_Time = 0.0f;
    m_Starting = false;
    std::cout << "[TitleScene] Init complete" << std::endl;
}

// ============================================================
// Shutdown
// ============================================================
void TitleScene::Shutdown()
{
    m_Text.Shutdown();
    m_Sprite.Shutdown();
    std::cout << "[TitleScene] Shutdown" << std::endl;
}

// ============================================================
// Update
// ============================================================
void TitleScene::Update(float dt)
{
    SceneBase::Update(dt);
    m_Time += dt;

    // 画面サイズの追従（リサイズ時）
    auto& gfx = Application::Get().GetGraphics();
    if (gfx.GetWidth() != m_ScreenW || gfx.GetHeight() != m_ScreenH)
    {
        m_ScreenW = gfx.GetWidth();
        m_ScreenH = gfx.GetHeight();
        m_Sprite.SetScreenSize(m_ScreenW, m_ScreenH);
        m_Camera.Init(45.0f, m_ScreenW / m_ScreenH, 0.1f, 10000.0f);
    }

    // カメラを原点の周りでゆっくり回す
    const float a = m_Time * kOrbitSpeed;
    m_Camera.LookAt(
        { std::sin(a) * kOrbitRadius, 6.0f, -std::cos(a) * kOrbitRadius },
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

    // 決定 → ゲームへ
    if (!m_Starting)
    {
        auto& input = InputManager::Get();
        const bool go = input.GetKeyTrigger(VK_RETURN)
            || input.GetKeyTrigger(VK_SPACE)
            || input.GetPadTrigger(XINPUT_GAMEPAD_A);
        if (go)
        {
            m_Starting = true;
            Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::COLLISION_TEST);
        }
    }
}

// ============================================================
// Render
// ============================================================
void TitleScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);

    m_Sprite.Begin();
    m_Text.Begin();

    // 暗幕。グリッドは薄く透けさせる
    if (m_WhiteTex)
        m_Sprite.Draw(m_WhiteTex, { 0.0f, 0.0f }, { m_ScreenW, m_ScreenH }, { 0.02f, 0.02f, 0.05f, 0.75f });

    // タイトル（中央やや上）
    const std::wstring title = L"VFX_L";
    const Vector2 titleSize = m_Text.Measure(title, kTitleScale);
    const Vector2 titlePos = { (m_ScreenW - titleSize.x) * 0.5f, m_ScreenH * 0.32f };
    m_Text.Draw(title, titlePos + Vector2(3.0f, 3.0f), { 0.0f, 0.0f, 0.0f, 0.8f }, kTitleScale);
    m_Text.Draw(title, titlePos, { 1.0f, 0.92f, 0.6f, 1.0f }, kTitleScale);

    // タイトル下の線
    if (m_WhiteTex)
    {
        const float lineW = titleSize.x + 80.0f;
        m_Sprite.Draw(m_WhiteTex,
            { (m_ScreenW - lineW) * 0.5f, titlePos.y + titleSize.y + 10.0f },
            { lineW, 2.0f }, { 1.0f, 0.92f, 0.6f, 0.8f });
    }

    // 開始の案内（点滅）
    const float blink = 0.55f + 0.45f * std::sin(m_Time * 3.0f);
    const std::wstring prompt = m_Starting ? L"Loading..." : L"Press Enter / Space to Start";
    const Vector2 promptSize = m_Text.Measure(prompt, kPromptScale);
    m_Text.Draw(prompt,
        { (m_ScreenW - promptSize.x) * 0.5f, m_ScreenH * 0.62f },
        { 1.0f, 1.0f, 1.0f, m_Starting ? 1.0f : blink }, kPromptScale);

    // 操作ヒント（右下）
    const std::wstring hint = L"F1: Game   F2: VFX Editor   F3: Title";
    const Vector2 hintSize = m_Text.Measure(hint, kHintScale);
    m_Text.Draw(hint,
        { m_ScreenW - hintSize.x - 24.0f, m_ScreenH - hintSize.y - 20.0f },
        { 0.7f, 0.7f, 0.7f, 0.9f }, kHintScale);

    m_Sprite.End();
    m_Text.End();
}
