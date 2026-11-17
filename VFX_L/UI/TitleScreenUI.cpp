// ============================================================
// TitleScreenUI.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "UI/TitleScreenUI.h"
#include "Graphics/Graphics.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/PostProcess/SceneSnapshot.h"
#include "UI/UIDeco.h"
#include "UI/InputGlyph.h"
#include "ResourcePaths.h"
#include "Core/DevUI.h"
#include "Core/VideoCapture.h"
#include "Manager/InputManager.h"

using namespace DirectX::SimpleMath;

namespace
{
    // ゲームの名前は仮。決まったらここだけ書き換える（英語表記）
    constexpr const wchar_t* kGameTitle = L"Rune Pack";

    constexpr float kFadeIn = 1.2f;   // 黒からふわっと出る秒数
    constexpr float kStartDelay = 0.7f;   // 決めてから場面を変えるまで（名前が吹き飛ぶのを見せる）

    float Smooth(float a, float b, float x)
    {
        const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // 暗い縁取り付きの文字（明るい空・草の上でも読める）
    void DrawOutlined(TextRenderer& text, const std::wstring& s, const Vector2& pos, const Vector4& color,
        float scale, float o)
    {
        const Vector4 shadow = { 0.0f, 0.0f, 0.0f, 0.75f * color.w };
        for (const Vector2 d : { Vector2(o, 0.0f), Vector2(-o, 0.0f), Vector2(0.0f, o), Vector2(0.0f, -o), Vector2(o * 1.5f, o * 1.5f) })
            text.Draw(s, pos + d, shadow, scale);
        text.Draw(s, pos, color, scale);
    }
}

bool TitleScreenUI::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, float screenW, float screenH)
{
    if (!m_Sprite.Initialize(device, context, 4096))   // 名前の小片（千枚ほど）+ 光の粒
        std::cout << "[Error] TitleScreenUI: SpriteRenderer init failed" << std::endl;
    if (!m_Text.Initialize(device, context, Res::Fnt::JP))
        std::cout << "[Error] TitleScreenUI: TextRenderer init failed" << std::endl;
    if (!m_TitleText.Initialize(device, context, Res::Fnt::Title))   // ゲーム名の字体
        std::cout << "[Error] TitleScreenUI: title font init failed" << std::endl;
    if (!m_MenuText.Initialize(device, context, Res::Fnt::Menu))   // メニューの字体（Zen Antique）
        std::cout << "[Error] TitleScreenUI: menu font init failed" << std::endl;

    m_White = std::make_shared<Texture>();
    if (!m_White->CreateSolid(device, 255, 255, 255, 255))
        m_White.reset();

    m_Menu.SetItems({ L"はじめる", L"トレーニング", L"遊び方", L"ゲームを終了" });   // トレーニング = 魔法の実験場、遊び方 = HowToPlayUI
    m_Menu.style = MenuList::Style::Text;
    // 項目の印のルーン：はじめる = 旅、トレーニング = 戦士、遊び方 = 知恵、終了 = 帰る所
    m_Menu.runes = { MenuList::kRuneRaido, MenuList::kRuneTiwaz, MenuList::kRuneAnsuz, MenuList::kRuneOthala };
    m_Table.Initialize();
    Layout(screenW, screenH);
    m_Menu.Open();

    m_Time = 0.0f;
    m_Starting = false;
    m_Ready = true;
    return true;
}

void TitleScreenUI::Shutdown()
{
    if (!m_Ready) return;
    m_MenuText.Shutdown();
    m_TitleText.Shutdown();
    m_Text.Shutdown();
    m_Sprite.Shutdown();
    m_Ready = false;
}

void TitleScreenUI::Layout(float screenW, float screenH)
{
    m_ScreenW = screenW;
    m_ScreenH = screenH;
    m_Sprite.SetScreenSize(screenW, screenH);
    m_K = (std::min)(screenW, screenH) / 900.0f * UIDeco::UIScale();   // UI 全体の倍率
    m_Left = screenW * 0.065f;

    // メニュー：文字だけの一覧。左の余白に揃え、画面の高さの 56% から
    const float s = (std::min)(screenW, screenH) * UIDeco::UIScale();
    const Vector2 item = { s * 0.39f, s * 0.065f };   // 1 項目（文字・ルーンも同じ割合で大きい）
    m_Menu.Layout({ m_Left - item.y * 0.32f, screenH * 0.49f }, item, s * 0.004f);   // 菱形の分だけ左へ（文字の頭を名前に揃える）
    m_Menu.stagger = item.y * 1.6f;   // 斜めに並べる（下ほど右）
    m_HowTo.Layout(screenW, screenH);
    m_Table.Layout(screenW, screenH, m_K);
}

int TitleScreenUI::Update(float dt)
{
    m_Time += dt;

    // ---- 名前の小片：マウスの動き（速さは少しならす）で散らす ----
    {
        const DirectX::XMFLOAT2 mp = InputManager::Get().GetMousePos();
        const Vector2 mouse(mp.x, mp.y);
        if (m_LastMouse.x >= 0.0f && dt > 0.0f)
        {
            const Vector2 v = (mouse - m_LastMouse) / dt;
            m_MouseVel += (v - m_MouseVel) * (std::min)(1.0f, dt * 20.0f);
        }
        m_LastMouse = mouse;
        if (InputManager::Get().IsUsingPad()) m_MouseVel = Vector2::Zero;
        m_Logo.Update(dt, m_LogoOrigin, mouse, m_MouseVel, m_K);
    }

    // ---- 右下の魔法の表（出てくる途中・決めた後・遊び方のページ中は触れない）----
    if (!m_Starting && !m_HowTo.IsOpen() && m_Time > kFadeIn && m_Table.Update(m_TableBp))
        m_SpellEdited = true;

    // ---- 決めた後：名前が吹き飛ぶのを見せてから返す ----
    if (m_PendingPick >= 0)
    {
        m_PickTimer -= dt;
        if (m_PickTimer > 0.0f) return kNone;
        const int picked = m_PendingPick;
        m_PendingPick = -1;
        return picked;
    }
    if (m_Starting || m_Time < kFadeIn * 0.5f) return kNone;   // 出てくる途中は受けない
    if (m_HowTo.IsOpen())   // 遊び方のページ：閉じるまでそちらだけ
    {
        m_HowTo.HandleInput(dt);
        return kNone;
    }
    const int picked = m_Menu.HandleInput(dt);
    if (picked == kStart || picked == kTraining)
    {
        m_Starting = true;
        m_Logo.Burst();
        m_PendingPick = picked;
        m_PickTimer = kStartDelay;
        return kNone;
    }
    if (picked == kHowTo)
    {
        m_HowTo.Open();
        return kNone;
    }
    return picked;
}

void TitleScreenUI::TestPick(int item)
{
    if (m_PendingPick >= 0) return;
    m_Starting = true;
    m_Logo.Burst();
    m_PendingPick = item;
    m_PickTimer = kStartDelay;
}

void TitleScreenUI::Render(Graphics& gfx)
{
    if (!m_Ready) return;
    ID3D11DeviceContext* ctx = gfx.GetContext();
    if (gfx.GetWidth() != m_ScreenW || gfx.GetHeight() != m_ScreenH)
        Layout(gfx.GetWidth(), gfx.GetHeight());

    // ---- 左側の暗い幕（景色は右へ抜ける）----
    {
        SceneSnapshot::Backdrop b;
        b.scrim = true;
        b.shadeStrength = 0.75f;
        b.shadeFrom = 0.12f;
        b.shadeTo = 0.58f;
        b.bottomShade = 0.35f;
        SceneSnapshot::Get().DrawBackdrop(ctx, gfx, b);
    }

    const float k = m_K;
    const float titleIn = Smooth(0.3f, 1.4f, m_Time);
    const float menuIn = Smooth(0.8f, 1.8f, m_Time);

    // ---- 名前の絵（後光・縁取り込み、明るさ 1）を一度だけ作って小片に切る。大きさが変わったら作り直す ----
    const std::wstring title = kGameTitle;
    const float titleScale = 0.80f * k;   // 96px の字体 → 約 77px × k
    const Vector2 titleSize = m_TitleText.Measure(title, titleScale);
    if (!m_Logo.Ready() || std::fabs(m_Logo.BuiltSize().x - titleSize.x) > 0.5f || std::fabs(m_Logo.BuiltSize().y - titleSize.y) > 0.5f)
    {
        m_Logo.Build(gfx.GetDevice(), ctx, titleSize, 12.0f * k, 5.0f * k, [&](const Vector2& at)
            {
                m_TitleText.Begin();
                const float g = 5.0f * k;
                const Vector4 halo = { 1.0f, 0.62f, 0.22f, 0.13f };
                for (int i = 0; i < 8; ++i)
                {
                    const float a = static_cast<float>(i) * DirectX::XM_2PI / 8.0f;
                    m_TitleText.Draw(title, at + Vector2(std::cos(a), std::sin(a)) * g, halo, titleScale);
                }
                DrawOutlined(m_TitleText, title, at, { 1.0f, 0.84f, 0.46f, 1.0f }, titleScale, 3.0f * k);
                m_TitleText.End();
            });
    }

    m_Sprite.Begin();
    m_Text.Begin();
    m_MenuText.Begin();
    m_TitleText.Begin();

    // ---- 景色の場面の切替の暗転（幕の上、名前とメニューの下）----
    if (m_Curtain > 0.0f && m_White)
        m_Sprite.Draw(m_White, { 0.0f, 0.0f }, { m_ScreenW, m_ScreenH }, { 0.0f, 0.0f, 0.0f, std::clamp(m_Curtain, 0.0f, 1.0f) });

    // ---- 名前（左上）→ 百合紋の分割線 ----
    // 専用字体 Mystery Quest（m_TitleText）+ 後ろで回る魔法陣（八芒星 + 内側に逆回転の花）
    // + 暖色の後光（8 方向にずらした薄い文字 = ぼかしの代わり）+ 暗い縁取り + HDR の金色（bloom で光る）。明るさはゆっくり息をする
    const float rise = (1.0f - titleIn) * 18.0f * k;   // 少し下から上がってくる
    const Vector2 titlePos = { m_Left, m_ScreenH * 0.12f + rise };
    const Vector2 titleCenter = titlePos + titleSize * 0.5f;
    const float breath = 0.5f + 0.5f * std::sin(m_Time * 1.3f);   // 0〜1
    if (m_White)
    {
        const float spin = UIDeco::Clock() * 0.18f;
        Vector4 ring = UIDeco::TintColor(UIDeco::Tint::Gold);
        ring.w = (0.20f + 0.08f * breath) * titleIn;
        const float d = titleSize.y * 3.0f;
        UIDeco::DrawCircle(m_Sprite, true, titleCenter, d, ring, spin);
        ring.w *= 0.7f;
        UIDeco::DrawCircle(m_Sprite, false, titleCenter, d * 0.62f, ring, -spin * 1.6f);
    }
    m_LogoOrigin = titlePos;
    const float hdr = 1.15f + 0.25f * breath;
    if (m_Logo.Ready())
        m_Logo.Draw(m_Sprite, titlePos, { hdr, hdr, hdr, titleIn }, UIDeco::Tex().disc);
    else
    {
        // 小片が作れなかった時：今までの描き方
        const float g = 5.0f * k;
        const Vector4 halo = { 1.0f, 0.62f, 0.22f, (0.10f + 0.05f * breath) * titleIn };
        for (int i = 0; i < 8; ++i)
        {
            const float a = static_cast<float>(i) * DirectX::XM_2PI / 8.0f;
            m_TitleText.Draw(title, titlePos + Vector2(std::cos(a), std::sin(a)) * g, halo, titleScale);
        }
        DrawOutlined(m_TitleText, title, titlePos, { 1.0f * hdr, 0.84f * hdr, 0.46f * hdr, titleIn }, titleScale, 3.0f * k);
    }

    const float y = titlePos.y + titleSize.y + 4.0f * k;
    if (m_White)
    {
        const float lineW = titleSize.x * 1.05f;
        const float lineH = lineW * 0.10f;
        Vector4 tint = UIDeco::TintColor(UIDeco::Tint::Gold);
        tint.w = titleIn;
        UIDeco::DrawDivider(m_Sprite, true, { m_Left + lineW * 0.5f, y + lineH * 0.5f }, lineW, tint);
    }

    // TEMP-TEST: 動画の書き出し中（作品紹介の動画のタイトル）：メニュー・案内の代わりに一言と作品の説明
    if (VideoCapture::Get().Enabled())
    {
        const float lineIn = Smooth(1.2f, 2.4f, m_Time);
        const float smallIn = Smooth(1.8f, 3.0f, m_Time);
        DrawOutlined(m_Text, L"並べ方で、魔法が化ける。", { m_Left, y + 26.0f * k }, { 1.0f, 0.93f, 0.78f, lineIn }, 0.78f * k, 2.0f * k);
        DrawOutlined(m_Text, L"3D サバイバーアクション　／　C++・DirectX 11 自作エンジン　／　1 人制作",
            { m_Left, y + 90.0f * k }, { 0.90f, 0.88f, 0.84f, 0.95f * smallIn }, 0.50f * k, 1.6f * k);
        const float black = 1.0f - Smooth(0.0f, kFadeIn, m_Time);
        if (black > 0.0f && m_White)
            m_Sprite.Draw(m_White, { 0.0f, 0.0f }, { m_ScreenW, m_ScreenH }, { 0.0f, 0.0f, 0.0f, black });
        m_Sprite.End();
        m_TitleText.End();
        m_MenuText.End();
        m_Text.End();
        return;
    }

    // ---- メニュー（決定後は「読み込み中」）----
    if (m_Starting)
    {
        const float blink = 0.6f + 0.4f * std::sin(m_Time * 4.0f);
        DrawOutlined(m_MenuText, L"読み込み中...", { m_Left, m_ScreenH * 0.58f }, { 1.0f, 0.92f, 0.70f, blink }, 0.48f * k, 1.5f * k);
    }
    else if (menuIn > 0.0f)
    {
        m_Menu.fade = menuIn;
        m_Menu.Draw(m_Sprite, m_MenuText, m_White, 0.645f * k);   // 40px の字体（行の高さ 57.9 = 毛筆体 32px の 1.25 倍）
    }

    // ---- 操作の案内（左下）と、開発用のシーン切替（右下）----
    const float hs = 0.34f * k;
    const std::wstring howTo = L"<{K:W} {K:S}|{DPADV}> で選択　　<{K:Enter}|{A}> で決定";   // <キーボード|パッド> の絵（InputGlyph）
    InputGlyph::DrawOutlined(m_Sprite, m_Text, howTo, { m_Left, m_ScreenH * 0.92f }, { 0.78f, 0.76f, 0.72f, 0.9f * menuIn }, hs, 1.0f * k);

    if constexpr (DevUI::kEnabled)   // Demo ビルドには F キーの切替が無い
    {
        const std::wstring dev = L"F1: Game   F2: VFX Editor   F3: Title";
        const float ds = 0.30f * k;
        const Vector2 devSize = m_Text.Measure(dev, ds);
        m_Text.Draw(dev, { m_ScreenW - devSize.x - 20.0f, m_ScreenH - devSize.y - 14.0f }, { 0.75f, 0.75f, 0.8f, 0.55f }, ds);
    }

    // ---- 右下の魔法の表 ----
    if (!m_Starting) m_Table.Draw(m_Sprite, m_Text, m_TableBp, menuIn);

    // ---- 遊び方のページ（メニューの上に重ねる）----
    if (m_HowTo.IsOpen()) m_HowTo.Draw(m_Sprite, m_Text, m_White);

    // ---- 起動直後：黒から出す（文字も含めて一番上）----
    const float black = 1.0f - Smooth(0.0f, kFadeIn, m_Time);
    if (black > 0.0f && m_White)
        m_Sprite.Draw(m_White, { 0.0f, 0.0f }, { m_ScreenW, m_ScreenH }, { 0.0f, 0.0f, 0.0f, black });

    m_Sprite.End();
    m_TitleText.End();   // 名前は他の文字より先（魔法陣の上、幕の上）
    m_MenuText.End();
    m_Text.End();
}
