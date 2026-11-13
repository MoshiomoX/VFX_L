// ============================================================
// MenuList.cpp
// ============================================================
#include "UI/MenuList.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Manager/InputManager.h"
#include "UI/UIDeco.h"
#include "Audio/AudioSystem.h"

using namespace DirectX::SimpleMath;

void MenuList::Layout(const Vector2& topLeft, const Vector2& itemSize, float gap)
{
    m_TopLeft = topLeft;
    m_ItemSize = itemSize;
    m_Gap = gap;
}

float MenuList::Height() const
{
    const int n = Count();
    return (n > 0) ? m_ItemSize.y * (float)n + m_Gap * (float)(n - 1) : 0.0f;
}

Vector2 MenuList::ItemPos(int i) const
{
    // stagger：下の項目ほど右へ（斜めの一直線。一番下が stagger だけ右）
    const int n = Count();
    const float slope = (n > 1) ? (float)i / (float)(n - 1) : 0.0f;
    return { m_TopLeft.x + stagger * slope, m_TopLeft.y + (m_ItemSize.y + m_Gap) * (float)i };
}

void MenuList::Open()
{
    m_Cursor = 0;
    m_InputDelay = 0.15f;
    m_LastMouse = { -1.0f, -1.0f };
    m_Stick.Reset();
}

// ============================================================
// マウスは「動かした時だけ」カーソルを奪う（キーで選んでいる途中に、
// 止まっているマウスの位置へ戻されないように）
// ============================================================
// 選択の移動・決定の音。判定そのものは HandleInputRaw
int MenuList::HandleInput(float dt)
{
    const int before = m_Cursor;
    const int picked = HandleInputRaw(dt);
    if (picked >= 0) AudioSystem::Get().Play("ui_select");
    else if (m_Cursor != before && before >= 0) AudioSystem::Get().Play("ui_hover");
    return picked;
}

int MenuList::HandleInputRaw(float dt)
{
    const int n = Count();
    if (n <= 0) return -1;

    auto& input = InputManager::Get();
    const auto mp = input.GetMousePos();
    const Vector2 mouse = { mp.x, mp.y };
    const bool mouseMoved = (mouse - m_LastMouse).LengthSquared() > 0.25f;
    m_LastMouse = mouse;
    m_Stick.Update(dt);   // 猶予の間も見ておく（出た時に倒したままなら、一度戻すまで動かない）

    if (m_InputDelay > 0.0f)
    {
        // 実時間で減らす（フレームレートで猶予が変わらないように）。
        // 1 フレームだけ長い時（読み込みの引っ掛かり）に猶予を食い尽くさないよう 0.1 秒で頭打ち
        m_InputDelay -= (std::min)((std::max)(dt, 0.0f), 0.1f);
        return -1;
    }

    int hover = -1;
    for (int i = 0; i < n; ++i)
    {
        const Vector2 p = ItemPos(i);
        if (mouse.x >= p.x && mouse.x <= p.x + m_ItemSize.x &&
            mouse.y >= p.y && mouse.y <= p.y + m_ItemSize.y)
        {
            hover = i;
            break;
        }
    }
    if (hover >= 0 && mouseMoved) m_Cursor = hover;
    if (m_Cursor < 0 || m_Cursor >= n) m_Cursor = 0;

    if (input.GetKeyTrigger(VK_UP) || input.GetKeyTrigger('W') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_UP) || m_Stick.Repeat(StickNav::kUp))
        m_Cursor = (m_Cursor + n - 1) % n;
    if (input.GetKeyTrigger(VK_DOWN) || input.GetKeyTrigger('S') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_DOWN) || m_Stick.Repeat(StickNav::kDown))
        m_Cursor = (m_Cursor + 1) % n;

    if (input.GetMouseTrigger(0) && hover >= 0)
    {
        m_Cursor = hover;
        return m_Cursor;
    }
    if (input.GetKeyTrigger(VK_RETURN) || input.GetKeyTrigger(VK_SPACE) || input.GetPadTrigger(XINPUT_GAMEPAD_A))
        return m_Cursor;

    return -1;
}

// ============================================================
// 選択中は色付きの地と枠、他は薄い地だけ
// ============================================================
void MenuList::Draw(SpriteRenderer& sprite, TextRenderer& text,
    const std::shared_ptr<Texture>& white, float textScale) const
{
    if (!white) return;
    if (style == Style::Text)
    {
        DrawTextStyle(sprite, text, white, textScale);
        return;
    }

    // 幻想 UI：暗い地 + 古金の細い二重線。選択中は線が明るく光り、両端に菱形
    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    for (int i = 0; i < Count(); ++i)
    {
        const Vector2 p = ItemPos(i);
        const bool sel = (i == m_Cursor);

        UIDeco::PanelStyle ps;
        ps.fill = sel ? Vector4(0.010f, 0.008f, 0.016f, 0.96f) : Vector4(0.003f, 0.0025f, 0.005f, 0.90f);
        ps.innerInset = 3.0f;
        ps.innerAlpha = sel ? 0.45f : 0.20f;
        Vector4 line = gold;
        if (!sel) line.w = 0.55f;
        UIDeco::DrawPanel(sprite, p, m_ItemSize, line, ps, sel ? 1.0f : 0.0f);

        if (sel)
        {
            const float g = m_ItemSize.y * 0.22f;
            const float cy = p.y + m_ItemSize.y * 0.5f;
            for (const float cx : { p.x, p.x + m_ItemSize.x })
                sprite.Draw(white, { cx - g * 0.5f, cy - g * 0.5f }, { g, g }, gold, 0.785398f, { cx, cy });
        }

        const Vector2 ls = text.Measure(m_Labels[i], textScale);
        text.Draw(m_Labels[i], { p.x + (m_ItemSize.x - ls.x) * 0.5f, p.y + (m_ItemSize.y - ls.y) * 0.5f },
            sel ? Vector4(0.96f, 0.92f, 0.84f, 1.0f) : Vector4(0.50f, 0.46f, 0.40f, 1.0f), textScale);
    }
}

// ============================================================
// 文字だけの一覧（タイトル）。背景の 3D の景色の上に直に置く。
// 選んでいる項目：左に古金の菱形、後ろに左から右へ消える金の光、文字の下に線。少し右へずれる。
// 文字は暗い縁取り（4 方向にずらした影）で、明るい空・草の上でも読めるように
// ============================================================
namespace
{
    // ルーンの線（輪の半径を 1 とした座標、y は下向き）。古フサルクの形
    struct Seg { float x0, y0, x1, y1; };
    const Seg kRaido[] = {    // ᚱ 旅
        { -0.22f, -0.55f, -0.22f, 0.55f }, { -0.22f, -0.55f, 0.24f, -0.25f }, { 0.24f, -0.25f, -0.22f, 0.02f },
        { -0.22f, 0.02f, 0.28f, 0.55f } };
    const Seg kTiwaz[] = {    // ᛏ 戦士
        { 0.0f, -0.55f, 0.0f, 0.55f }, { 0.0f, -0.55f, -0.34f, -0.15f }, { 0.0f, -0.55f, 0.34f, -0.15f } };
    const Seg kAnsuz[] = {    // ᚨ 知恵
        { -0.18f, -0.55f, -0.18f, 0.55f }, { -0.18f, -0.55f, 0.28f, -0.25f }, { -0.18f, -0.15f, 0.28f, 0.15f } };
    const Seg kOthala[] = {   // ᛟ 帰る所
        { -0.30f, -0.16f, 0.0f, -0.55f }, { 0.0f, -0.55f, 0.30f, -0.16f },
        { -0.30f, -0.16f, 0.34f, 0.55f }, { 0.30f, -0.16f, -0.34f, 0.55f } };
    struct RuneShape { const Seg* segs; int count; };
    const RuneShape kRunes[MenuList::kRuneCount] = {
        { kRaido, 4 }, { kTiwaz, 3 }, { kAnsuz, 3 }, { kOthala, 4 } };
}

void MenuList::DrawRune(SpriteRenderer& sprite, const std::shared_ptr<Texture>& white, int rune,
    const Vector2& c, float r, float e, float now) const
{
    const auto& tex = UIDeco::Tex();
    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);

    // 選んだ時：後ろで小さな魔法陣がゆっくり回り、柔らかい光が灯る
    if (e > 0.01f)
    {
        if (tex.disc)
        {
            const float g = r * 2.6f;
            sprite.Draw(tex.disc, { c.x - g * 0.5f, c.y - g * 0.5f }, { g, g }, { gold.x, gold.y, gold.z, 0.10f * e * fade });
        }
        if (tex.circleStar)
        {
            const float d = r * 3.0f;
            sprite.Draw(tex.circleStar, { c.x - d * 0.5f, c.y - d * 0.5f }, { d, d },
                { gold.x * 1.4f, gold.y * 1.4f, gold.z * 1.4f, 0.55f * e * fade }, now * 0.6f, c);
        }
    }
    // 輪
    if (tex.ring)
    {
        const float d = r * 2.0f;
        const float b = 0.8f + 0.9f * e;   // 選ぶと HDR で光る
        sprite.Draw(tex.ring, { c.x - d * 0.5f, c.y - d * 0.5f }, { d, d },
            { gold.x * b, gold.y * b, gold.z * b, (0.45f + 0.55f * e) * fade });
    }
    // 文字の線（細い長方形を線の向きへ回す）
    const RuneShape& shape = kRunes[rune];
    const float thick = (std::max)(1.5f, r * 0.13f);
    const float pulse = 1.0f + 0.15f * std::sin(now * 3.0f);
    const float b = 0.55f + (1.4f * pulse - 0.55f) * e;
    const Vector4 col = { gold.x * b, gold.y * b, gold.z * b, (0.55f + 0.45f * e) * fade };
    for (int k = 0; k < shape.count; ++k)
    {
        const Seg& s = shape.segs[k];
        const Vector2 a = c + Vector2(s.x0, s.y0) * r, z = c + Vector2(s.x1, s.y1) * r;
        const Vector2 d = z - a;
        const float len = d.Length() + thick;   // 端を少し伸ばして角をつなぐ
        const Vector2 mid = (a + z) * 0.5f;
        sprite.Draw(white, { mid.x - len * 0.5f, mid.y - thick * 0.5f }, { len, thick }, col, std::atan2(d.y, d.x), mid);
    }
}

void MenuList::DrawTextStyle(SpriteRenderer& sprite, TextRenderer& text,
    const std::shared_ptr<Texture>& white, float textScale) const
{
    const int n = Count();
    if ((int)m_SelAnim.size() != n) m_SelAnim.assign(n, 0.0f);
    const float now = UIDeco::Clock();
    const float dt = (m_AnimClock < 0.0f) ? 0.0f : (std::min)(0.1f, now - m_AnimClock);
    m_AnimClock = now;

    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    for (int i = 0; i < n; ++i)
    {
        float& s = m_SelAnim[i];
        const float target = (i == m_Cursor) ? 1.0f : 0.0f;
        s += (target - s) * (std::min)(1.0f, dt * 12.0f);
        const float e = s * s * (3.0f - 2.0f * s);

        const Vector2 p = ItemPos(i);
        const float h = m_ItemSize.y;

        // 後ろの光（左が濃く、右へ消える。細い帯を並べて段々に）
        if (e > 0.01f)
        {
            constexpr int kStrips = 16;
            const float w = m_ItemSize.x;
            for (int k = 0; k < kStrips; ++k)
            {
                const float t = (float)k / (float)kStrips;
                const float a = 0.30f * e * (1.0f - t) * (1.0f - t) * fade;
                sprite.Draw(white, { p.x + w * t, p.y + h * 0.08f }, { w / (float)kStrips + 1.0f, h * 0.84f },
                    { gold.x, gold.y, gold.z, a });
            }
        }

        // 左の印：ルーン（runes で指定）か菱形
        const int rune = (i < (int)runes.size()) ? runes[i] : -1;
        if (rune >= 0 && rune < kRuneCount)
            DrawRune(sprite, white, rune, { p.x + h * 0.36f, p.y + h * 0.5f }, h * 0.30f, e, now);
        else
        {
            const float g = h * (0.16f + 0.08f * e);
            const Vector2 c = { p.x + h * 0.32f, p.y + h * 0.5f };
            Vector4 col = gold;
            col.w = (0.35f + 0.65f * e) * fade;
            sprite.Draw(white, { c.x - g * 0.5f, c.y - g * 0.5f }, { g, g }, col, 0.785398f, c);   // 回転の中心はピクセル
        }

        // 文字（暗い縁取り → 本体）。ルーンの時は印が大きいので少し右から
        const std::wstring& label = m_Labels[i];
        const Vector2 ls = text.Measure(label, textScale);
        const float textLeft = (rune >= 0) ? 0.86f : 0.70f;
        const Vector2 tp = { p.x + h * textLeft + h * 0.22f * e, p.y + (h - ls.y) * 0.5f };
        const float o = (std::max)(1.0f, h * 0.035f);
        const Vector4 shadow = { 0.0f, 0.0f, 0.0f, 0.75f * fade };
        for (const Vector2 d : { Vector2(o, 0.0f), Vector2(-o, 0.0f), Vector2(0.0f, o), Vector2(0.0f, -o), Vector2(o, o) })
            text.Draw(label, tp + d, shadow, textScale);
        const Vector4 dim = { 0.70f, 0.64f, 0.55f, 0.90f };
        const Vector4 lit = { 1.00f, 0.90f, 0.62f, 1.00f };
        Vector4 col = Vector4::Lerp(dim, lit, e);
        col.w *= fade;
        text.Draw(label, tp, col, textScale);

        // 下の線（選んだ時に左から伸びる）
        if (e > 0.01f)
        {
            Vector4 line = gold;
            line.w = 0.9f * e * fade;
            sprite.Draw(white, { tp.x, tp.y + ls.y + h * 0.02f }, { ls.x * e, (std::max)(1.0f, h * 0.03f) }, line);
        }
    }
}
