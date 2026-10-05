// ============================================================
// TitleScene.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "Scene/TitleScene.h"
#include "Scene/RunResult.h"
#include "Core/Application.h"
#include "Graphics/Material/Texture.h"
#include "Manager/InputManager.h"
#include "ResourcePaths.h"
#include "UI/UIDeco.h"
#include "Audio/AudioSystem.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

using namespace DirectX::SimpleMath;

namespace
{
    // ゲームの名前は仮。決まったらここだけ書き換える
    constexpr const wchar_t* kGameTitle = L"ルーンパック";
    constexpr const wchar_t* kTagline = L"魔法をバックパックに詰めて、押し寄せる群れを生き抜け";

    constexpr float kOrbitRadius = 14.0f;
    constexpr float kOrbitSpeed = 0.15f;   // rad / 秒
    constexpr int   kDecoCount = 28;

    // アイテムの色（火球・分裂・二重詠唱・HP・MP・枠）に寄せた飾りの色
    const Vector4 kDecoColors[] = {
        { 1.00f, 0.55f, 0.20f, 1.0f },
        { 0.30f, 0.90f, 0.90f, 1.0f },
        { 0.75f, 0.45f, 1.00f, 1.0f },
        { 0.85f, 0.25f, 0.25f, 1.0f },
        { 0.30f, 0.50f, 0.95f, 1.0f },
        { 0.95f, 0.80f, 0.25f, 1.0f },
    };
}

// ============================================================
// Init
// ============================================================
void TitleScene::Init()
{
    std::cout << "[TitleScene] Init" << std::endl;
    g_RunCarry.Reset();   // タイトルからは第 1 面を最初から（面の引き継ぎを捨てる）

    auto& gfx = Application::Get().GetGraphics();
    auto* device = gfx.GetDevice();
    auto* context = gfx.GetContext();

    m_ScreenW = gfx.GetWidth();
    m_ScreenH = gfx.GetHeight();

    // 背景用のカメラ。グリッドを少し見下ろす
    m_Camera.Init(45.0f, m_ScreenW / m_ScreenH, 0.1f, 10000.0f);
    m_Camera.LookAt({ 0.0f, 6.0f, -kOrbitRadius }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    SetCamera(&m_Camera);

    if (!m_Sprite.Initialize(device, context, 256))
        std::cout << "[Error] TitleScene: SpriteRenderer init failed" << std::endl;
    m_Sprite.SetScreenSize(m_ScreenW, m_ScreenH);

    if (!m_Text.Initialize(device, context, Res::Fnt::JP))
        std::cout << "[Error] TitleScene: TextRenderer init failed" << std::endl;

    m_WhiteTex = std::make_shared<Texture>();
    if (!m_WhiteTex->CreateSolid(device, 255, 255, 255, 255))
        m_WhiteTex.reset();

    m_Menu.SetItems({ L"はじめる", L"ゲームを終了" });
    Layout();
    m_Menu.Open();
    InitDecos();

    m_Time = 0.0f;
    m_Starting = false;
    AudioSystem::Get().PlayMusic("title");   // 戦闘から戻った時は面の曲から交差フェード

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

// メニューの位置（画面短辺に比例）
void TitleScene::Layout()
{
    const float s = (std::min)(m_ScreenW, m_ScreenH);
    const Vector2 item = { s * 0.34f, s * 0.07f };
    m_Menu.Layout({ (m_ScreenW - item.x) * 0.5f, m_ScreenH * 0.58f }, item, s * 0.018f);
}

// 飾りの四角を画面全体にばらまく（毎回同じ並び）
void TitleScene::InitDecos()
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    m_Decos.clear();
    for (int i = 0; i < kDecoCount; ++i)
    {
        Deco d;
        d.pos = { u01(rng) * m_ScreenW, u01(rng) * m_ScreenH };
        d.size = 10.0f + u01(rng) * 34.0f;
        d.speed = 12.0f + u01(rng) * 30.0f;
        d.angle = u01(rng) * 6.2831853f;
        d.spin = (u01(rng) - 0.5f) * 1.2f;
        d.color = kDecoColors[i % (int)std::size(kDecoColors)];
        d.color.w = 0.10f + u01(rng) * 0.18f;
        m_Decos.push_back(d);
    }
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
        Layout();
    }

    // カメラを原点の周りでゆっくり回す
    const float a = m_Time * kOrbitSpeed;
    m_Camera.LookAt(
        { std::sin(a) * kOrbitRadius, 6.0f, -std::cos(a) * kOrbitRadius },
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

    // 飾り：上へ漂い、上に抜けたら下から戻る
    for (auto& d : m_Decos)
    {
        d.pos.y -= d.speed * dt;
        d.angle += d.spin * dt;
        if (d.pos.y < -d.size * 2.0f) d.pos.y = m_ScreenH + d.size * 2.0f;
    }

    // メニュー
    if (!m_Starting)
    {
        switch (m_Menu.HandleInput())
        {
        case 0:
            m_Starting = true;
            Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::COLLISION_TEST);
            break;
        case 1:
            PostQuitMessage(0);   // 窓の × と同じ終わり方（Esc での即終了は 2026-10-04 に外した）
            break;
        default:
            break;
        }
    }
}

// ============================================================
// Render
// ============================================================
void TitleScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);

    const float k = (std::min)(m_ScreenW, m_ScreenH) / 900.0f;

    m_Sprite.Begin();
    m_Text.Begin();

    if (m_WhiteTex)
    {
        // 暗幕。グリッドは薄く透けさせる
        m_Sprite.Draw(m_WhiteTex, { 0.0f, 0.0f }, { m_ScreenW, m_ScreenH }, { 0.02f, 0.02f, 0.05f, 0.75f });

        // 漂う四角
        for (const auto& d : m_Decos)
            m_Sprite.Draw(m_WhiteTex, { d.pos.x - d.size * 0.5f, d.pos.y - d.size * 0.5f },
                { d.size, d.size }, d.color, d.angle, d.pos);
    }

    // ---- 名前（中央やや上）と一言 ----
    const std::wstring title = kGameTitle;
    const float titleScale = 1.6f * k;
    const Vector2 titleSize = m_Text.Measure(title, titleScale);
    const Vector2 titlePos = { (m_ScreenW - titleSize.x) * 0.5f, m_ScreenH * 0.24f };

    // 名前の後ろで大きな魔法陣がゆっくり回る（幻想 UI、古金）
    {
        const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
        Vector4 ring = gold;
        ring.w = 0.22f;
        UIDeco::DrawCircle(m_Sprite, true, { m_ScreenW * 0.5f, titlePos.y + titleSize.y * 0.55f },
            m_ScreenH * 0.62f, ring, UIDeco::Clock() * 0.03f);
    }
    m_Text.Draw(title, titlePos + Vector2(3.0f, 3.0f) * k, { 0.0f, 0.0f, 0.0f, 0.8f }, titleScale);
    m_Text.Draw(title, titlePos, { 1.0f, 0.92f, 0.6f, 1.0f }, titleScale);

    // 名前の下に百合紋の分割線
    float y = titlePos.y + titleSize.y + 2.0f * k;
    {
        const float lineW = titleSize.x + 160.0f * k;
        const float lineH = lineW * 0.10f;
        UIDeco::DrawDivider(m_Sprite, true, { m_ScreenW * 0.5f, y + lineH * 0.5f }, lineW,
            UIDeco::TintColor(UIDeco::Tint::Gold));
        y += lineH + 4.0f * k;
    }

    const std::wstring tagline = kTagline;
    const float tagScale = 0.5f * k;
    const Vector2 tagSize = m_Text.Measure(tagline, tagScale);
    m_Text.Draw(tagline, { (m_ScreenW - tagSize.x) * 0.5f, y }, { 0.85f, 0.85f, 0.9f, 0.9f }, tagScale);

    // ---- メニュー（決定後は「読み込み中」に差し替え）----
    if (m_Starting)
    {
        const std::wstring loading = L"読み込み中...";
        const float ls = 0.55f * k;
        const Vector2 lsz = m_Text.Measure(loading, ls);
        m_Text.Draw(loading, { (m_ScreenW - lsz.x) * 0.5f, m_ScreenH * 0.62f }, { 1, 1, 1, 1 }, ls);
    }
    else
    {
        m_Menu.Draw(m_Sprite, m_Text, m_WhiteTex, 0.55f * k);
    }

    // ---- 操作の案内（下の中央）と、開発用のシーン切替（右下）----
    const std::wstring howTo = L"W / S で選択    Enter / Space / パッド A で決定";
    const float hs = 0.36f * k;
    const Vector2 howSize = m_Text.Measure(howTo, hs);
    m_Text.Draw(howTo, { (m_ScreenW - howSize.x) * 0.5f, m_ScreenH * 0.90f }, { 0.7f, 0.7f, 0.75f, 0.9f }, hs);

    const std::wstring dev = L"F1: Game   F2: VFX Editor   F3: Title";
    const float ds = 0.32f * k;
    const Vector2 devSize = m_Text.Measure(dev, ds);
    m_Text.Draw(dev, { m_ScreenW - devSize.x - 20.0f, m_ScreenH - devSize.y - 14.0f },
        { 0.5f, 0.5f, 0.55f, 0.7f }, ds);

    m_Sprite.End();
    m_Text.End();
}
