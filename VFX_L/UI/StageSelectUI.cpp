// ============================================================
// StageSelectUI.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "UI/StageSelectUI.h"
#include "Graphics/Graphics.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/PostProcess/SceneSnapshot.h"
#include "Manager/InputManager.h"
#include "Audio/AudioSystem.h"
#include "Core/GameProgress.h"
#include "UI/UIDeco.h"
#include "UI/InputGlyph.h"
#include "ResourcePaths.h"

#include <Xinput.h>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kOpenDelay = 0.3f;    // 開いてから決定を受けるまで（秒）
    constexpr float kSwitchDelay = 0.12f; // マップのカーソルが止まってから景色を切り替えるまで（秒。預けてある地形を出すだけなので短い）

    // マップの一言と、未解放の時の条件（Megabonk の「Beginner-Friendly / Advanced Challenge」に倣う）
    const wchar_t* MapBlurb(int map)
    {
        switch (map)
        {
        case 1:  return L"初心者向け　段々の台地と草原、スプリッターの群れ";
        case 2:  return L"上級　開けた砂地、砂嵐の術師とトゲ玉";
        default: return L"最難　松明の明かりだけの遺跡、盾兵の群れ";
        }
    }
    const wchar_t* MapUnlockHint(int map)
    {
        switch (map)
        {
        case 2:  return L"草原の Tier 2 で 2 面目（砂漠）に入ると解放";
        case 3:  return L"砂漠の Tier 2 か 草原の Tier 3 で 遺跡に入ると解放";
        default: return L"";
        }
    }

    float Smooth(float a, float b, float x)
    {
        const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    void DrawOutlined(TextRenderer& text, const std::wstring& s, const Vector2& pos, const Vector4& color,
        float scale, float o)
    {
        const Vector4 shadow = { 0.0f, 0.0f, 0.0f, 0.75f * color.w };
        for (const Vector2 d : { Vector2(o, 0.0f), Vector2(-o, 0.0f), Vector2(0.0f, o), Vector2(0.0f, -o), Vector2(o * 1.5f, o * 1.5f) })
            text.Draw(s, pos + d, shadow, scale);
        text.Draw(s, pos, color, scale);
    }
}

bool StageSelectUI::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, float screenW, float screenH)
{
    if (!m_Sprite.Initialize(device, context, 512))
        std::cout << "[Error] StageSelectUI: SpriteRenderer init failed" << std::endl;
    if (!m_Text.Initialize(device, context, Res::Fnt::JP))
        std::cout << "[Error] StageSelectUI: TextRenderer init failed" << std::endl;
    m_White = std::make_shared<Texture>();
    if (!m_White->CreateSolid(device, 255, 255, 255, 255))
        m_White.reset();

    Layout(screenW, screenH);
    m_Time = 0.0f;
    m_InputDelay = kOpenDelay;
    m_Starting = false;
    m_MapChanged = false;
    m_Ready = true;
    return true;
}

void StageSelectUI::Shutdown()
{
    if (!m_Ready) return;
    m_Text.Shutdown();
    m_Sprite.Shutdown();
    m_Ready = false;
}

void StageSelectUI::Layout(float screenW, float screenH)
{
    m_ScreenW = screenW;
    m_ScreenH = screenH;
    m_Sprite.SetScreenSize(screenW, screenH);
    m_K = (std::min)(screenW, screenH) / 900.0f * UIDeco::UIScale();
    m_Left = screenW * 0.065f;

    // マップの行：見出しの下に 3 行。Tier の箱はその下。矩形は Render で描く所と同じ式
    const float rowW = screenW * 0.36f;
    const float rowH = screenH * 0.105f;
    const float y0 = screenH * 0.21f;
    for (int i = 0; i < StageConfig::kStageCount; ++i)
        m_MapRows[i] = { { m_Left - 10.0f * m_K, y0 + rowH * i }, { rowW, rowH - 6.0f * m_K } };

    const float box = 46.0f * m_K;
    const float tierY = y0 + rowH * StageConfig::kStageCount + screenH * 0.035f;
    const float tierX = m_Left + 90.0f * m_K;
    for (int t = 0; t < StageConfig::kMaxTier; ++t)
        m_TierBoxes[t] = { { tierX + (box + 16.0f * m_K) * t, tierY }, { box, box } };

    const float btnY = screenH * 0.80f;
    m_StartBtn = { { m_Left - 8.0f * m_K, btnY }, { 190.0f * m_K, 44.0f * m_K } };
    m_BackBtn = { { m_Left + 200.0f * m_K, btnY }, { 170.0f * m_K, 44.0f * m_K } };
}

void StageSelectUI::SetSelection(int map, int tier)
{
    m_Map = std::clamp(map, 1, StageConfig::kStageCount);
    m_Tier = std::clamp(tier, 1, StageConfig::MaxTier(m_Map));
}

void StageSelectUI::SelectMap(int map)
{
    map = std::clamp(map, 1, StageConfig::kStageCount);
    if (map == m_Map) return;
    m_Map = map;
    m_Tier = std::clamp(m_Tier, 1, StageConfig::MaxTier(m_Map));
    // 開いている一番高い Tier を初期値に（Megabonk は前回の選択を覚えるが、閉じた Tier を指したままにはしない）
    const auto& p = GameProgress::Get();
    while (m_Tier > 1 && !p.IsTierOpen(m_Map, m_Tier)) --m_Tier;
    m_MapChanged = true;
    m_ChangeTimer = 0.0f;
    AudioSystem::Get().Play("ui_hover");
}

bool StageSelectUI::ConsumeMapChanged()
{
    if (!m_MapChanged || m_ChangeTimer < kSwitchDelay) return false;
    m_MapChanged = false;
    return true;
}

void StageSelectUI::SelectTier(int tier)
{
    tier = std::clamp(tier, 1, StageConfig::MaxTier(m_Map));
    if (tier == m_Tier) return;
    m_Tier = tier;
    AudioSystem::Get().Play("ui_hover");
}

void StageSelectUI::MoveMap(int dir) { SelectMap(m_Map + dir); }
void StageSelectUI::MoveTier(int dir) { SelectTier(m_Tier + dir); }

bool StageSelectUI::TryStart()
{
    const auto& p = GameProgress::Get();
    if (!p.IsMapUnlocked(m_Map) || !p.IsTierOpen(m_Map, m_Tier))
    {
        AudioSystem::Get().Play("gold_deny");
        m_DenyFlash = 0.6f;
        return false;
    }
    AudioSystem::Get().Play("ui_select");
    m_Starting = true;
    return true;
}

int StageSelectUI::Update(float dt)
{
    m_Time += dt;
    m_ChangeTimer += dt;
    if (m_DenyFlash > 0.0f) m_DenyFlash -= dt;
    if (m_Starting) return kNone;
    auto& input = InputManager::Get();

    if (input.GetPadTrigger(XINPUT_GAMEPAD_B) || input.GetKeyTrigger(VK_ESCAPE) || input.GetKeyTrigger(VK_BACK))
    {
        AudioSystem::Get().Play("ui_close");
        return kBack;
    }
    if (m_InputDelay > 0.0f)
    {
        m_InputDelay -= dt;
        return kNone;
    }

    const bool up = input.GetKeyTrigger('W') || input.GetKeyTrigger(VK_UP) || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_UP);
    const bool down = input.GetKeyTrigger('S') || input.GetKeyTrigger(VK_DOWN) || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_DOWN);
    const bool left = input.GetKeyTrigger('A') || input.GetKeyTrigger(VK_LEFT) || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_LEFT)
        || input.GetPadTrigger(XINPUT_GAMEPAD_LEFT_SHOULDER);
    const bool right = input.GetKeyTrigger('D') || input.GetKeyTrigger(VK_RIGHT) || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_RIGHT)
        || input.GetPadTrigger(XINPUT_GAMEPAD_RIGHT_SHOULDER);
    const bool confirm = input.GetKeyTrigger(VK_RETURN) || input.GetKeyTrigger(VK_SPACE) || input.GetPadTrigger(XINPUT_GAMEPAD_A);
    if (up) MoveMap(-1);
    else if (down) MoveMap(+1);
    else if (left) MoveTier(-1);
    else if (right) MoveTier(+1);
    else if (confirm) return TryStart() ? kStart : kNone;

    // ---- マウス：hover では選ばない（触れる度に地形を作り直すと重い。2026-10-21）。クリックで行・箱・ボタン ----
    const DirectX::XMFLOAT2 mp = input.GetMousePos();
    const Vector2 mouse(mp.x, mp.y);
    m_LastMouse = mouse;
    if (input.GetMouseTrigger(0))
    {
        for (int i = 0; i < StageConfig::kStageCount; ++i)
            if (m_MapRows[i].Contains(mouse)) SelectMap(i + 1);
        for (int t = 0; t < StageConfig::MaxTier(m_Map); ++t)
            if (m_TierBoxes[t].Contains(mouse)) SelectTier(t + 1);
        if (m_StartBtn.Contains(mouse)) return TryStart() ? kStart : kNone;
        if (m_BackBtn.Contains(mouse))
        {
            AudioSystem::Get().Play("ui_close");
            return kBack;
        }
    }
    return kNone;
}

// Tier の箱の下の一行
std::wstring StageSelectUI::TierDescription(int tier) const
{
    const auto& p = GameProgress::Get();
    wchar_t buf[200];
    if (!p.IsMapUnlocked(m_Map)) return MapUnlockHint(m_Map);
    if (!p.IsTierOpen(m_Map, tier))
    {
        swprintf_s(buf, L"Tier %d をクリアすると解放", tier - 1);
        return buf;
    }
    // 続く面の名前：草原 → 砂漠 → 遺跡
    std::wstring chain;
    for (int i = 0; i < tier; ++i)
    {
        if (i > 0) chain += L" → ";
        chain += StageConfig::Get(m_Map + i).name;
    }
    const auto& tm = StageConfig::TierMul(tier);
    if (tier == 1)
        swprintf_s(buf, L"%ls の 1 面だけ　Boss を倒せばクリア", chain.c_str());
    else
        swprintf_s(buf, L"%ls の %d 面を続けて　敵の HP・攻撃 ×%.2f　経験値 ×%.1f", chain.c_str(), tier, tm.hpMul, tm.expMul);
    return buf;
}

void StageSelectUI::Render(Graphics& gfx)
{
    if (!m_Ready) return;
    ID3D11DeviceContext* ctx = gfx.GetContext();
    if (gfx.GetWidth() != m_ScreenW || gfx.GetHeight() != m_ScreenH)
        Layout(gfx.GetWidth(), gfx.GetHeight());

    // ---- 左側の暗い幕（タイトルと同じ）----
    {
        SceneSnapshot::Backdrop b;
        b.scrim = true;
        b.shadeStrength = 0.78f;
        b.shadeFrom = 0.12f;
        b.shadeTo = 0.60f;
        b.bottomShade = 0.35f;
        SceneSnapshot::Get().DrawBackdrop(ctx, gfx, b);
    }

    const float k = m_K;
    const float fadeIn = Smooth(0.0f, 0.5f, m_Time);
    const auto& p = GameProgress::Get();
    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    const Vector4 silver = UIDeco::TintColor(UIDeco::Tint::Silver);

    m_Sprite.Begin();
    m_Text.Begin();

    // ---- 見出し ----
    {
        const std::wstring header = L"ステージ選択";
        const float hs = 0.95f * k;
        const Vector2 size = m_Text.Measure(header, hs);
        const Vector2 pos = { m_Left, m_ScreenH * 0.08f };
        DrawOutlined(m_Text, header, pos, { 1.0f, 0.86f, 0.52f, fadeIn }, hs, 3.0f * k);
        if (m_White)
        {
            const float lineW = (std::max)(size.x, 300.0f * k);
            Vector4 tint = gold;
            tint.w = fadeIn;
            UIDeco::DrawDivider(m_Sprite, true, { m_Left + lineW * 0.5f, pos.y + size.y + 4.0f * k + lineW * 0.05f }, lineW, tint);
        }
    }

    // ---- マップの行 ----
    for (int i = 0; i < StageConfig::kStageCount; ++i)
    {
        const int map = i + 1;
        const Rect& r = m_MapRows[i];
        const bool sel = (map == m_Map);
        const bool open = p.IsMapUnlocked(map);
        if (m_White)
        {
            // 下敷き：選んでいる行は古金の枠を濃く、少し明るい地
            const Vector4 fill = sel ? Vector4(0.030f, 0.024f, 0.012f, 0.90f) : Vector4(0.006f, 0.005f, 0.009f, 0.80f);
            UIDeco::DrawTextPlate(m_Sprite, r.pos, r.size, { 0.0f, 0.0f }, fill, (sel ? 0.85f : 0.22f) * fadeIn);
            if (sel)
            {
                // 左の菱形（MenuList の Text 風）
                const float d = 10.0f * k;
                Vector4 c = gold; c.w = fadeIn;
                m_Sprite.Draw(m_White, { r.pos.x - d * 1.4f - d * 0.5f, r.pos.y + r.size.y * 0.5f - d * 0.5f }, { d, d }, c, DirectX::XM_PIDIV4, { 0.5f, 0.5f });
            }
        }
        const float dim = open ? 1.0f : 0.45f;
        const Vector4 nameColor = sel ? Vector4(1.0f, 0.92f, 0.70f, fadeIn) : Vector4(0.80f * dim, 0.78f * dim, 0.74f * dim, fadeIn);
        const float ns = 0.60f * k;
        const Vector2 namePos = { r.pos.x + 22.0f * k, r.pos.y + 6.0f * k };
        std::wstring name = StageConfig::Get(map).name;
        if (!open) name += L"　（未解放）";
        DrawOutlined(m_Text, name, namePos, nameColor, ns, 2.0f * k);
        // クリア済みの Tier の★（開始マップとしての記録）
        if (open)
        {
            std::wstring stars;
            for (int t = 1; t <= StageConfig::MaxTier(map); ++t) stars += p.IsTierCleared(map, t) ? L"★" : L"☆";
            Vector4 sc = gold; sc.w = 0.85f * fadeIn;
            m_Text.Draw(stars, { r.pos.x + r.size.x - m_Text.Measure(stars, 0.36f * k).x - 14.0f * k, r.pos.y + 10.0f * k }, sc, 0.36f * k);
        }
        const std::wstring blurb = open ? MapBlurb(map) : MapUnlockHint(map);
        const Vector4 bc = { 0.70f * dim, 0.68f * dim, 0.64f * dim, 0.95f * fadeIn };
        m_Text.Draw(blurb, { namePos.x, namePos.y + m_Text.GetLineHeight(ns) - 2.0f * k }, bc, 0.30f * k);
    }

    // ---- Tier の箱 ----
    {
        const Rect& first = m_TierBoxes[0];
        const float ls = 0.52f * k;
        DrawOutlined(m_Text, L"Tier", { m_Left, first.pos.y + (first.size.y - m_Text.GetLineHeight(ls)) * 0.5f }, { 0.92f, 0.88f, 0.80f, fadeIn }, ls, 2.0f * k);
        const int maxTier = StageConfig::MaxTier(m_Map);
        for (int t = 0; t < StageConfig::kMaxTier; ++t)
        {
            const int tier = t + 1;
            const Rect& b = m_TierBoxes[t];
            const bool exists = tier <= maxTier;
            const bool open = exists && p.IsTierOpen(m_Map, tier);
            const bool sel = exists && (tier == m_Tier);
            if (!exists) continue;   // そのマップから続けられる面が無い（遺跡は 1 だけ）
            if (m_White)
            {
                Vector4 frame = open ? gold : silver;
                frame.w = (open ? 0.95f : 0.35f) * fadeIn;
                if (sel)
                {
                    Vector4 fill = gold;
                    fill.w = (0.85f + 0.10f * std::sin(m_Time * 4.0f)) * fadeIn;
                    if (!open && m_DenyFlash > 0.0f) fill = { 0.9f, 0.12f, 0.08f, fadeIn };
                    m_Sprite.Draw(m_White, b.pos, b.size, fill);
                }
                else
                    m_Sprite.Draw(m_White, b.pos, b.size, { 0.006f, 0.005f, 0.009f, 0.75f * fadeIn });
                UIDeco::DrawFrameLines(m_Sprite, b.pos, b.size, frame, sel ? 2.0f : 1.0f);
            }
            wchar_t num[8];
            swprintf_s(num, L"%d", tier);
            const float ns2 = 0.52f * k;
            const Vector2 sz = m_Text.Measure(num, ns2);
            Vector4 nc = sel ? Vector4(0.03f, 0.02f, 0.01f, fadeIn)
                : (open ? Vector4(0.95f, 0.90f, 0.75f, fadeIn) : Vector4(0.40f, 0.38f, 0.36f, fadeIn));
            m_Text.Draw(num, { b.pos.x + (b.size.x - sz.x) * 0.5f, b.pos.y + (b.size.y - sz.y) * 0.5f }, nc, ns2);
            if (!open)
            {
                // 鍵：箱の右上に小さく
                m_Text.Draw(L"鍵", { b.pos.x + b.size.x - 14.0f * k, b.pos.y - 4.0f * k }, { 0.75f, 0.70f, 0.60f, 0.8f * fadeIn }, 0.22f * k);
            }
            else if (p.IsTierCleared(m_Map, tier))
            {
                Vector4 sc = gold; sc.w = fadeIn;
                m_Text.Draw(L"★", { b.pos.x + b.size.x - 14.0f * k, b.pos.y - 6.0f * k }, sc, 0.26f * k);
            }
        }
        // 説明の一行
        const std::wstring desc = TierDescription(m_Tier);
        const Vector4 dc = m_DenyFlash > 0.0f ? Vector4(1.0f, 0.35f, 0.30f, fadeIn) : Vector4(0.84f, 0.82f, 0.78f, 0.95f * fadeIn);
        DrawOutlined(m_Text, desc, { m_Left, first.pos.y + first.size.y + 12.0f * k }, dc, 0.32f * k, 1.5f * k);
    }

    // ---- 出発 / 戻る ----
    {
        const bool canStart = p.IsMapUnlocked(m_Map) && p.IsTierOpen(m_Map, m_Tier);
        auto button = [&](const Rect& r, const std::wstring& label, bool enabled)
            {
                if (m_White)
                    UIDeco::DrawTextPlate(m_Sprite, r.pos, r.size, { 0.0f, 0.0f }, { 0.012f, 0.010f, 0.016f, 0.92f }, (enabled ? 0.9f : 0.3f) * fadeIn);
                const float s = 0.38f * k;
                const Vector2 sz = InputGlyph::Measure(m_Text, label, s);
                const Vector4 c = enabled ? Vector4(1.0f, 0.92f, 0.70f, fadeIn) : Vector4(0.55f, 0.52f, 0.48f, fadeIn);
                InputGlyph::DrawOutlined(m_Sprite, m_Text, label, { r.pos.x + (r.size.x - sz.x) * 0.5f, r.pos.y + (r.size.y - m_Text.GetLineHeight(s)) * 0.5f }, c, s, 1.5f * k);
            };
        button(m_StartBtn, L"<{K:Enter}|{A}>  出発", canStart);
        button(m_BackBtn, L"<{K:Esc}|{B}>  戻る", true);
    }

    // ---- 操作の案内（左下）----
    {
        const float hs = 0.32f * k;
        const std::wstring howTo = L"<{K:W} {K:S}|{DPADV}> ステージ　　<{K:A} {K:D}|{DPADH}> Tier";
        InputGlyph::DrawOutlined(m_Sprite, m_Text, howTo, { m_Left, m_ScreenH * 0.92f }, { 0.78f, 0.76f, 0.72f, 0.9f * fadeIn }, hs, 1.0f * k);
    }

    m_Sprite.End();
    m_Text.End();
}
