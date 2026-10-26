// ============================================================
// ResultScene.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "Scene/ResultScene.h"
#include "Scene/RunResult.h"
#include "World/StageConfig.h"
#include "Core/Application.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/PostProcess/SceneSnapshot.h"
#include "Item/ItemDatabase.h"
#include "Manager/InputManager.h"
#include "Audio/AudioSystem.h"
#include "ResourcePaths.h"
#include "UI/UIDeco.h"
#include "UI/InputGlyph.h"

using namespace DirectX::SimpleMath;

namespace
{
    // 出てくる順番（秒）
    constexpr float kFadeIn = 0.5f;        // 黒から写真へ
    constexpr float kHeaderIn = 0.15f;
    constexpr float kPanelIn = 0.35f;
    constexpr float kRowStart = 0.55f;     // 1 行目が数え始める
    constexpr float kRowStep = 0.12f;      // 次の行まで
    constexpr float kCountTime = 0.7f;     // 1 行の数え上げ
    constexpr float kBagIn = 0.9f;
    constexpr float kAllShown = kRowStart + kRowStep * 5.0f + kCountTime;

    float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    float Smooth(float a, float b, float x)
    {
        const float t = Clamp01((x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }
    float EaseOut(float t) { t = Clamp01(t); const float u = 1.0f - t; return 1.0f - u * u * u; }

    // 3 桁ごとのカンマ
    std::wstring WithCommas(long long v)
    {
        std::wstring s = std::to_wstring(v < 0 ? -v : v);
        for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, L",");
        return (v < 0) ? L"-" + s : s;
    }

    // 暗い縁取り付きの文字（写真の明るい所の上でも読める）
    void DrawOutlined(TextRenderer& text, const std::wstring& s, const Vector2& pos, const Vector4& color,
        float scale, float o)
    {
        const Vector4 shadow = { 0.0f, 0.0f, 0.0f, 0.75f * color.w };
        for (const Vector2 d : { Vector2(o, 0.0f), Vector2(-o, 0.0f), Vector2(0.0f, o), Vector2(0.0f, -o), Vector2(o * 1.5f, o * 1.5f) })
            text.Draw(s, pos + d, shadow, scale);
        text.Draw(s, pos, color, scale);
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

    // 3D は描かない（背景は写真）。SceneBase が点光源を上げるのにカメラが要る
    m_Camera.Init(45.0f, m_ScreenW / m_ScreenH, 0.1f, 1000.0f);
    m_Camera.LookAt({ 0.0f, 10.0f, -18.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    SetCamera(&m_Camera);

    if (!m_Sprite.Initialize(device, context, 2048))
        std::cout << "[Error] ResultScene: SpriteRenderer init failed" << std::endl;
    if (!m_Text.Initialize(device, context, Res::Fnt::JP))
        std::cout << "[Error] ResultScene: TextRenderer init failed" << std::endl;

    m_White = std::make_shared<Texture>();
    if (!m_White->CreateSolid(device, 255, 255, 255, 255))
        m_White.reset();

    // 今回のビルド：バックパックの描き方そのまま（アイコンは ItemDatabase から）
    ItemDatabase::Initialize();
    m_BackpackView.Initialize(m_White);
    m_BackpackView.LoadIcons();

    BuildRows();
    Layout();

    m_Time = 0.0f;
    m_Leaving = false;
    m_Hover = -1;
    std::cout << "[ResultScene] Init complete" << std::endl;
}

// ============================================================
// 表の中身。valid でなければ全部「--」
// ============================================================
void ResultScene::BuildRows()
{
    const RunResult& r = g_LastRun;
    const bool v = r.valid;
    m_Rows[0] = { L"生存時間", r.survivedSec, true, v };
    m_Rows[1] = { L"到達レベル", (float)r.level, false, v };
    m_Rows[2] = { L"撃破数", (float)r.kills, false, v };
    m_Rows[3] = { L"獲得金貨", (float)r.goldEarned, false, v };
    m_Rows[4] = { L"獲得経験値", r.expGained, false, v };
    m_Rows[5] = { L"開けた宝箱", (float)r.cratesOpened, false, v };

    const bool next = r.valid && r.hasNextStage;
    m_Buttons[0].label = next ? L"次のステージへ" : L"ステージ選択へ";   // 2026-10-21：終わったらステージ選択（Megabonk の hub に戻るのと同じ）
    m_Buttons[0].key = L"<{K:Enter}|{A}>";   // <キーボード|パッド> の絵（InputGlyph。機器が替わったら Layout し直す）
    m_Buttons[1].label = L"タイトルへ";
    m_Buttons[1].key = L"<{K:BackSpace}|{B}>";
}

std::wstring ResultScene::FormatValue(const Row& r, float shown) const
{
    if (!r.valid) return r.time ? L"--:--" : L"--";
    if (r.time)
    {
        const int total = (int)shown;
        wchar_t buf[32];
        swprintf_s(buf, L"%02d:%02d", total / 60, total % 60);
        return buf;
    }
    return WithCommas((long long)(shown + 0.5f));
}

// ============================================================
// 画面から位置を決める（1600x900 で UI 倍率 1.5 の時に収まる比率）
// ============================================================
void ResultScene::Layout()
{
    m_LayoutPad = InputGlyph::UsingPad();
    m_Sprite.SetScreenSize(m_ScreenW, m_ScreenH);
    m_K = (std::min)(m_ScreenW, m_ScreenH) / 900.0f * UIDeco::UIScale();

    const float x0 = m_ScreenW * 0.06f;
    m_StatsPos = { x0, m_ScreenH * 0.285f };
    m_StatsSize = { m_ScreenW * 0.25f, m_ScreenH * 0.52f };

    // ビルドの見出しの分だけ下げて、表と同じ下端に揃える
    const float caption = m_Text.GetLineHeight(0.40f * m_K) + 6.0f * m_K;
    m_BagExtent = m_StatsSize.y - caption;
    m_BagPos = { m_StatsPos.x + m_StatsSize.x + m_ScreenW * 0.022f, m_StatsPos.y + caption };
    m_BackpackView.PlaceAt(m_BagPos, m_BagExtent);

    // 下のボタン（左寄せ、横に 2 つ）
    const float labelScale = 0.50f * m_K;
    const float keyScale = 0.32f * m_K;
    const Vector2 pad = { 18.0f * m_K, 8.0f * m_K };
    float x = x0;
    const float y = m_ScreenH * 0.865f;
    for (Button& b : m_Buttons)
    {
        const Vector2 ks = InputGlyph::Measure(m_Text, b.key, keyScale);
        const Vector2 ls = m_Text.Measure(b.label, labelScale);
        b.pos = { x, y };
        b.size = { pad.x * 2.0f + ks.x + 14.0f * m_K + ls.x, pad.y * 2.0f + (std::max)(ks.y, ls.y) };
        x += b.size.x + 20.0f * m_K;
    }
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

void ResultScene::Leave(int button)
{
    if (m_Leaving) return;
    m_Leaving = true;
    AudioSystem::Get().Play("ui_select");
    auto& sm = Application::Get().GetGame().GetSceneManager();
    if (button == 0)
    {
        // クリアして次の面があれば、戦闘シーンが積んだ引き継ぎ（g_RunCarry）のまま商店へ（2026-10-15。商店から次の面へ）。
        // 無ければ最初から
        if (g_LastRun.hasNextStage)
            sm.RequestChangeScene(SceneType::SHOP);
        else
        {
            g_RunCarry.Reset();
            sm.RequestChangeScene(SceneType::STAGE_SELECT);
        }
    }
    else
    {
        g_RunCarry.Reset();
        sm.RequestChangeScene(SceneType::TITLE);
    }
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
        m_Camera.Init(45.0f, m_ScreenW / m_ScreenH, 0.1f, 1000.0f);
        Layout();
    }
    // ボタンのキーの絵はキーボード / パッドで幅が違う。機器が替わったら測り直す
    if (InputGlyph::UsingPad() != m_LayoutPad) Layout();
    if (m_Leaving) return;

    {
        // TEMP-TEST: VFXL_RESULT_AUTOLEAVE=<0..1> で 5 秒後にそのボタン（0 = 次へ / もう一度、1 = タイトルへ）
        char env[8] = {};
        if (m_Time > 5.0f && GetEnvironmentVariableA("VFXL_RESULT_AUTOLEAVE", env, sizeof(env)) > 0)
        {
            Leave(std::clamp(atoi(env), 0, 1));
            return;
        }
    }

    auto& input = InputManager::Get();
    const bool confirm = input.GetKeyTrigger(VK_RETURN) || input.GetKeyTrigger(VK_SPACE) || input.GetPadTrigger(XINPUT_GAMEPAD_A);
    const bool back = input.GetKeyTrigger(VK_BACK) || input.GetPadTrigger(XINPUT_GAMEPAD_B);

    // 出てくる途中に押したら最後まで飛ばす（連打で読み飛ばさないよう、その押下では決めない）
    if (m_Time < kAllShown)
    {
        if ((confirm || back || input.GetMouseTrigger(0)) && m_Time > 0.3f) m_Time = kAllShown;
        return;
    }

    // マウス：ボタンの上で光る・押す
    const auto mp = input.GetMousePos();
    const Vector2 mouse = { mp.x, mp.y };
    if ((mouse - m_LastMouse).LengthSquared() > 0.25f || input.GetMouseTrigger(0))
    {
        m_Hover = -1;
        for (int i = 0; i < 2; ++i)
        {
            const Button& b = m_Buttons[i];
            if (mouse.x >= b.pos.x && mouse.x <= b.pos.x + b.size.x && mouse.y >= b.pos.y && mouse.y <= b.pos.y + b.size.y)
                m_Hover = i;
        }
    }
    m_LastMouse = mouse;

    if (input.GetMouseTrigger(0) && m_Hover >= 0) Leave(m_Hover);
    else if (confirm) Leave(0);
    else if (back) Leave(1);
}

// ============================================================
// Render
// ============================================================
void ResultScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);
    auto& gfx = Application::Get().GetGraphics();
    const float k = m_K;
    const RunResult& r = g_LastRun;
    const SceneSnapshot& snap = SceneSnapshot::Get();

    // ---- 背景：爆発の一コマ（真ん中を右へ寄せ、左を暗く）----
    {
        SceneSnapshot::Backdrop b;
        b.photoCenter = { 0.70f, 0.5f };
        b.zoom = 0.70f;   // 真ん中を 0.70 へ寄せても左端が写真の外へ出ない（0.5 / 0.70 より小さく）
        b.brightness = 0.95f;
        b.shadeStrength = 0.80f;
        b.shadeFrom = 0.30f;
        b.shadeTo = 0.68f;
        b.vignette = 0.40f;
        b.bottomShade = 0.40f;
        SceneSnapshot::Get().DrawBackdrop(gfx.GetContext(), gfx, b);
    }

    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    const Vector4 silver = UIDeco::TintColor(UIDeco::Tint::Silver);
    const float headerIn = Smooth(kHeaderIn, kHeaderIn + 0.4f, m_Time);
    const float panelIn = Smooth(kPanelIn, kPanelIn + 0.4f, m_Time);
    const float bagIn = Smooth(kBagIn, kBagIn + 0.5f, m_Time);
    const float buttonsIn = Smooth(kAllShown - 0.3f, kAllShown + 0.1f, m_Time);

    m_Sprite.Begin();
    m_Text.Begin();

    const float x0 = m_ScreenW * 0.06f;

    // ---- 見出し：ステージクリア（金）/ 力尽きた（赤）+ 百合紋 + 面の名前 ----
    {
        std::wstring header = L"リザルト";
        Vector4 color = { 1.0f, 0.86f, 0.52f, headerIn };
        wchar_t tierBuf[32] = {};
        if (r.valid && r.cleared)
        {
            // Tier を最後まで戦い抜いた時はその Tier のクリア（2026-10-21、ステージ選択）。途中の面は「ステージクリア」
            if (!r.hasNextStage && r.tier > 1) { swprintf_s(tierBuf, L"Tier %d クリア", r.tier); header = tierBuf; }
            else header = L"ステージクリア";
        }
        else if (r.valid)
        {
            header = L"力尽きた";
            color = { 0.95f, 0.22f, 0.16f, headerIn };
        }
        const float hs = 1.1f * k;
        const Vector2 size = m_Text.Measure(header, hs);
        const Vector2 pos = { x0 - (1.0f - headerIn) * 30.0f * k, m_ScreenH * 0.06f };
        DrawOutlined(m_Text, header, pos, color, hs, 3.0f * k);

        float y = pos.y + size.y + 2.0f * k;
        const float lineW = (std::max)(size.x, 300.0f * k);
        Vector4 tint = gold;
        tint.w = headerIn;
        UIDeco::DrawDivider(m_Sprite, true, { x0 + lineW * 0.5f, y + lineW * 0.05f }, lineW, tint);
        y += lineW * 0.10f + 4.0f * k;

        if (r.valid)
        {
            const StageDef& def = StageConfig::Get(std::clamp(r.stage, 1, StageConfig::kStageCount));
            wchar_t buf[64];
            swprintf_s(buf, L"%ls　Tier %d　ステージ %d / %d", def.name, r.tier, r.pos, r.tier);
            DrawOutlined(m_Text, buf, { x0, y }, { 0.86f, 0.84f, 0.80f, headerIn }, 0.42f * k, 1.5f * k);
        }
    }

    // ---- 戦績の表 ----
    if (panelIn > 0.0f)
    {
        UIDeco::PanelStyle ps;
        ps.fill = { 0.0025f, 0.0020f, 0.0045f, 0.90f * panelIn };
        ps.cornerSize = 26.0f * k;
        ps.cornerAlpha = 0.9f * panelIn;
        ps.innerAlpha = 0.40f * panelIn;
        Vector4 line = gold;
        line.w = panelIn;
        UIDeco::DrawPanel(m_Sprite, m_StatsPos, m_StatsSize, line, ps);

        const float padX = 22.0f * k;
        float y = m_StatsPos.y + 16.0f * k;
        const float titleScale = 0.44f * k;
        m_Text.Draw(L"戦績", { m_StatsPos.x + padX, y }, { gold.x, gold.y, gold.z, panelIn }, titleScale);
        y += m_Text.GetLineHeight(titleScale) + 2.0f * k;
        Vector4 thin = gold;
        thin.w = 0.5f * panelIn;
        UIDeco::DrawDivider(m_Sprite, false, { m_StatsPos.x + m_StatsSize.x * 0.5f, y + 4.0f * k }, m_StatsSize.x - padX * 2.0f, thin);
        y += 14.0f * k;

        const float rowsBottom = m_StatsPos.y + m_StatsSize.y - 14.0f * k;
        const float rowH = (rowsBottom - y) / (float)kRowCount;
        const float labelScale = 0.40f * k;
        const float valueScale = 0.54f * k;
        for (int i = 0; i < kRowCount; ++i)
        {
            const float start = kRowStart + kRowStep * (float)i;
            const float a = Smooth(start - 0.1f, start + 0.2f, m_Time);
            if (a <= 0.0f) break;
            const Row& row = m_Rows[i];
            const float shown = row.value * EaseOut((m_Time - start) / kCountTime);
            const float slide = (1.0f - a) * 20.0f * k;

            const Vector2 ls = m_Text.Measure(row.label, labelScale);
            m_Text.Draw(row.label, { m_StatsPos.x + padX + slide, y + (rowH - ls.y) * 0.5f },
                { 0.74f, 0.70f, 0.62f, a }, labelScale);
            const std::wstring value = FormatValue(row, shown);
            const Vector2 vs = m_Text.Measure(value, valueScale);
            m_Text.Draw(value, { m_StatsPos.x + m_StatsSize.x - padX - vs.x + slide, y + (rowH - vs.y) * 0.5f },
                { 1.0f, 0.97f, 0.90f, a }, valueScale);

            if (i < kRowCount - 1)
            {
                Vector4 sep = silver;
                sep.w = 0.18f * a;
                m_Sprite.Draw(m_White, { m_StatsPos.x + padX, y + rowH }, { m_StatsSize.x - padX * 2.0f, 1.0f }, sep);
            }
            y += rowH;
        }
    }

    // ---- 今回のビルド（終わった時のバックパック）----
    if (bagIn > 0.0f && r.valid)
    {
        const float cs = 0.40f * k;
        DrawOutlined(m_Text, L"今回のビルド", { m_BagPos.x, m_StatsPos.y }, { gold.x, gold.y, gold.z, bagIn }, cs, 1.5f * k);
        m_BackpackView.Draw(m_Sprite, r.backpack);
        // 出てくる間は暗い幕を上に被せて薄く見せる（BackpackUI に濃さの引数は無い）
        if (bagIn < 1.0f && m_White)
            m_Sprite.Draw(m_White, { m_BagPos.x - 2.0f, m_BagPos.y - 2.0f }, { m_BagExtent + 4.0f, m_BagExtent + 4.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f - bagIn });
    }

    // ---- 写真の時刻（右下）----
    if (snap.Has() && r.valid)
    {
        const int t = (int)snap.shotTime;
        wchar_t buf[48];
        swprintf_s(buf, snap.highlight ? L"ハイライト　%02d:%02d" : L"最後の瞬間　%02d:%02d", t / 60, t % 60);
        const float s = 0.34f * k;
        const Vector2 sz = m_Text.Measure(buf, s);
        const Vector2 pos = { m_ScreenW - sz.x - 28.0f * k, m_ScreenH * 0.93f };
        DrawOutlined(m_Text, buf, pos, { 0.92f, 0.88f, 0.80f, 0.85f * headerIn }, s, 1.2f * k);
        // 左に小さな菱形
        const float g = sz.y * 0.32f;
        const Vector2 c = { pos.x - g * 1.4f, pos.y + sz.y * 0.5f };
        Vector4 col = gold;
        col.w = 0.9f * headerIn;
        m_Sprite.Draw(m_White, { c.x - g * 0.5f, c.y - g * 0.5f }, { g, g }, col, 0.785398f, c);
    }

    // ---- 下のボタン（読み込み中はその文字）----
    if (m_Leaving)
    {
        const float blink = 0.6f + 0.4f * std::sin(m_Time * 4.0f);
        DrawOutlined(m_Text, L"読み込み中...", m_Buttons[0].pos, { 1.0f, 0.92f, 0.70f, blink }, 0.5f * k, 1.5f * k);
    }
    else if (buttonsIn > 0.0f)
    {
        const float labelScale = 0.50f * k;
        const float keyScale = 0.32f * k;
        const Vector2 pad = { 18.0f * k, 8.0f * k };
        for (int i = 0; i < 2; ++i)
        {
            const Button& b = m_Buttons[i];
            const bool hot = (i == m_Hover) || (m_Hover < 0 && i == 0);   // マウスが無ければ Enter の方を強く
            const float pulse = hot ? 0.5f + 0.5f * std::sin(m_Time * 3.0f) : 0.0f;

            UIDeco::PanelStyle ps;
            ps.fill = { 0.004f, 0.003f, 0.007f, 0.92f * buttonsIn };
            ps.innerInset = 3.0f;
            ps.innerAlpha = (hot ? 0.45f : 0.20f) * buttonsIn;
            Vector4 line = gold;
            line.w = (hot ? 1.0f : 0.55f) * buttonsIn;
            UIDeco::DrawPanel(m_Sprite, b.pos, b.size, line, ps, hot ? 0.6f + 0.4f * pulse : 0.0f);

            const Vector2 ks = InputGlyph::Measure(m_Text, b.key, keyScale);
            const Vector2 ls = m_Text.Measure(b.label, labelScale);
            const float cy = b.pos.y + b.size.y * 0.5f;
            InputGlyph::Draw(m_Sprite, m_Text, b.key, { b.pos.x + pad.x, cy - ks.y * 0.5f }, { gold.x, gold.y, gold.z, 0.9f * buttonsIn }, keyScale);
            m_Text.Draw(b.label, { b.pos.x + pad.x + ks.x + 14.0f * k, cy - ls.y * 0.5f },
                hot ? Vector4(1.0f, 0.95f, 0.82f, buttonsIn) : Vector4(0.78f, 0.74f, 0.66f, buttonsIn), labelScale);
        }
    }

    // ---- 黒から出す（写真と枠。文字はそれぞれ自分で薄く出る）----
    const float black = 1.0f - Smooth(0.0f, kFadeIn, m_Time);
    if (black > 0.0f && m_White)
        m_Sprite.Draw(m_White, { 0.0f, 0.0f }, { m_ScreenW, m_ScreenH }, { 0.0f, 0.0f, 0.0f, black });

    m_Sprite.End();
    m_Text.End();
}
