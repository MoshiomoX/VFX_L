// ============================================================
// ShopScene.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "Scene/ShopScene.h"
#include "Scene/RunResult.h"
#include "World/StageConfig.h"
#include "Core/Application.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/PostProcess/SceneSnapshot.h"
#include "Item/ItemDatabase.h"
#include "Item/ItemInfo.h"
#include "Manager/InputManager.h"
#include "Manager/ResourceManager.h"
#include "Audio/AudioSystem.h"
#include "ResourcePaths.h"
#include "UI/UIDeco.h"
#include "UI/InputGlyph.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kFadeIn = 0.4f;   // 黒から写真へ

    float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    float Smooth(float a, float b, float x)
    {
        const float t = Clamp01((x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }

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

    bool Inside(const Vector2& p, const Vector2& pos, const Vector2& size)
    {
        return p.x >= pos.x && p.x <= pos.x + size.x && p.y >= pos.y && p.y <= pos.y + size.y;
    }

    std::wstring NameOf(ItemID id)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) return L"?";
        if (c->displayName && c->displayName[0]) return c->displayName;
        std::string n = c->name ? c->name : "?";
        return std::wstring(n.begin(), n.end());
    }
}

// ============================================================
// Init
// ============================================================
void ShopScene::Init()
{
    std::cout << "[ShopScene] Init" << std::endl;

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
        std::cout << "[Error] ShopScene: SpriteRenderer init failed" << std::endl;
    if (!m_Text.Initialize(device, context, Res::Fnt::JP))
        std::cout << "[Error] ShopScene: TextRenderer init failed" << std::endl;

    m_White = std::make_shared<Texture>();
    if (!m_White->CreateSolid(device, 255, 255, 255, 255))
        m_White.reset();

    ItemDatabase::Initialize();
    m_Cards.Initialize(m_White);
    m_Cards.SetWhiteTexture(m_White);
    m_Cards.LoadIcons();

    // 一覧の行のアイコン（能力値の札も並ぶので両方の一覧から）
    m_Icons.clear();
    auto load = [&](const std::vector<ItemID>& ids)
        {
            for (ItemID id : ids)
            {
                const ItemCommon* c = ItemDatabase::GetCommon(id);
                if (!c || !c->iconPath) continue;
                if (auto tex = ResourceManager::Get().LoadTexture(c->iconPath)) m_Icons.push_back({ id, tex });
            }
        };
    load(ItemDatabase::GetAllIDs());
    load(ItemDatabase::GetLevelUpOnlyIDs());
    m_GoldIcon = ResourceManager::Get().LoadTexture(Res::Icon::Gold);

    m_Stock.Generate(g_RunCarry);
    m_LastCoins = g_RunCarry.wallet.Coins();

    char env[8] = {};
    m_AutoTest = GetEnvironmentVariableA("VFXL_SHOP_AUTOTEST", env, sizeof(env)) > 0;   // TEMP-TEST
    m_AutoStep = 0;

    Layout();
    m_Time = 0.0f;
    m_Leaving = false;
    m_Cursor = 0;
    m_HoverRow = -1;
    m_HoverButton = -1;
    m_InputDelay = 0.3f;
    m_Msg.clear();
    m_MsgTime = 0.0f;
    AudioSystem::Get().Play("ui_open");
    std::cout << "[ShopScene] Init complete (gold " << m_LastCoins << ", stock " << m_Stock.Count() << ")" << std::endl;
}

void ShopScene::Shutdown()
{
    m_Text.Shutdown();
    m_Sprite.Shutdown();
    std::cout << "[ShopScene] Shutdown" << std::endl;
}

std::shared_ptr<Texture> ShopScene::GetIcon(ItemID id) const
{
    for (const auto& p : m_Icons)
        if (p.first == id) return p.second;
    return nullptr;
}

// ============================================================
// 画面から位置を決める（1600x900 で UI 倍率 1.5 の時に収まる比率）
// ============================================================
void ShopScene::Layout()
{
    m_LayoutPad = InputGlyph::UsingPad();
    m_Sprite.SetScreenSize(m_ScreenW, m_ScreenH);
    m_K = (std::min)(m_ScreenW, m_ScreenH) / 900.0f * UIDeco::UIScale();
    const float k = m_K;

    m_Cards.fitCards = 4;   // 四択と同じ大きさのカード
    m_Cards.Layout(m_ScreenW, m_ScreenH);
    const Vector2 cs = m_Cards.CardSize();

    const float x0 = m_ScreenW * 0.06f;
    const int rows = ShopStock::kSlots;
    m_RowH = 44.0f * k;
    m_RowGap = 5.0f * k;
    m_ListPad = 14.0f * k;
    m_ListPos = { x0, m_ScreenH * 0.26f };
    m_ListSize = { m_ScreenW * 0.36f, m_ListPad * 2.0f + m_RowH * (float)rows + m_RowGap * (float)(rows - 1) };

    // 右のカード。下にはみ出すなら上へ
    m_CardPos = { m_ListPos.x + m_ListSize.x + m_ScreenW * 0.035f, m_ScreenH * 0.22f };
    const float maxBottom = m_ScreenH * 0.96f;
    if (m_CardPos.y + cs.y > maxBottom) m_CardPos.y = (std::max)(m_ScreenH * 0.10f, maxBottom - cs.y);

    BuildButtons();
}

Vector2 ShopScene::RowPos(int i) const
{
    return { m_ListPos.x + m_ListPad, m_ListPos.y + m_ListPad + (m_RowH + m_RowGap) * (float)i };
}

void ShopScene::BuildButtons()
{
    const float k = m_K;
    m_Buttons[kBuy].label = L"買う";
    m_Buttons[kBuy].key = L"<{K:Enter}|{A}>";
    wchar_t buf[64];
    swprintf_s(buf, L"品揃えを変える　金貨 %d", m_Stock.RerollCost());
    m_Buttons[kReroll].label = buf;
    m_Buttons[kReroll].key = L"<{K:R}|{X}>";
    m_Buttons[kLeave].label = L"次のステージへ";
    m_Buttons[kLeave].key = L"<{K:BackSpace}|{B}>";

    const float labelScale = 0.44f * k;
    const float keyScale = 0.30f * k;
    const Vector2 pad = { 14.0f * k, 7.0f * k };
    float x = m_ListPos.x;
    const float y = m_ListPos.y + m_ListSize.y + 16.0f * k;
    for (Button& b : m_Buttons)
    {
        const Vector2 ks = InputGlyph::Measure(m_Text, b.key, keyScale);
        const Vector2 ls = m_Text.Measure(b.label, labelScale);
        b.pos = { x, y };
        b.size = { pad.x * 2.0f + ks.x + 12.0f * k + ls.x, pad.y * 2.0f + (std::max)(ks.y, ls.y) };
        x += b.size.x + 14.0f * k;
    }
}

// ============================================================
// 操作
// ============================================================
void ShopScene::SetMessage(const std::wstring& s, bool good)
{
    m_Msg = s;
    m_MsgGood = good;
    m_MsgTime = 2.2f;
}

void ShopScene::TryBuy()
{
    if (m_Stock.Count() <= 0) return;
    const ShopStock::Slot& s = m_Stock.At(m_Cursor);
    if (s.sold)
    {
        AudioSystem::Get().Play("gold_deny");
        SetMessage(L"売り切れ", false);
        return;
    }
    if (!g_RunCarry.wallet.CanPay(s.price))
    {
        AudioSystem::Get().Play("gold_deny");
        wchar_t buf[64];
        swprintf_s(buf, L"金貨が足りない　%d / %d", g_RunCarry.wallet.Coins(), s.price);
        SetMessage(buf, false);
        return;
    }
    const std::wstring name = NameOf(s.id);
    if (!m_Stock.Buy(g_RunCarry, m_Cursor))
    {
        AudioSystem::Get().Play("gold_deny");
        return;
    }
    AudioSystem::Get().Play("gold_spend");
    SetMessage(name + L" を買った", true);
    std::cout << "[ShopScene] bought slot " << m_Cursor << " for " << s.price << " gold" << std::endl;
}

void ShopScene::TryReroll()
{
    const int cost = m_Stock.RerollCost();
    if (!m_Stock.Reroll(g_RunCarry))
    {
        AudioSystem::Get().Play("gold_deny");
        wchar_t buf[64];
        swprintf_s(buf, L"金貨が足りない　%d / %d", g_RunCarry.wallet.Coins(), cost);
        SetMessage(buf, false);
        return;
    }
    AudioSystem::Get().Play("card_reroll");
    SetMessage(L"品揃えを変えた", true);
    BuildButtons();   // 次の値段で測り直す
}

void ShopScene::Leave()
{
    if (m_Leaving) return;
    m_Leaving = true;
    AudioSystem::Get().Play("ui_select");
    // 引き継ぎ（g_RunCarry）は戦闘シーンが積んだまま。買った物も入っている
    Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::COLLISION_TEST);
}

// ============================================================
// Update
// ============================================================
void ShopScene::Update(float dt)
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
    if (InputGlyph::UsingPad() != m_LayoutPad) Layout();

    if (m_MsgTime > 0.0f) m_MsgTime -= dt;
    if (m_GoldFlash > 0.0f) m_GoldFlash = (std::max)(0.0f, m_GoldFlash - dt * 2.5f);
    const int coins = g_RunCarry.wallet.Coins();
    if (coins != m_LastCoins) { m_GoldFlash = 1.0f; m_LastCoins = coins; }

    if (m_Leaving) return;
    if (m_AutoTest) AutoTest();
    if (m_Leaving) return;

    if (m_InputDelay > 0.0f)
    {
        m_InputDelay -= dt;
        return;
    }

    auto& input = InputManager::Get();
    const int n = m_Stock.Count();

    // ---- 行の移動（W S / 上下 / 十字）----
    if (n > 0)
    {
        int move = 0;
        if (input.GetKeyTrigger(VK_UP) || input.GetKeyTrigger('W') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_UP)) move = -1;
        if (input.GetKeyTrigger(VK_DOWN) || input.GetKeyTrigger('S') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_DOWN)) move = 1;
        if (move != 0)
        {
            m_Cursor = (m_Cursor + n + move) % n;
            AudioSystem::Get().Play("ui_hover");
        }
    }

    // ---- マウス：行とボタンの上で光る、クリックで選ぶ / 押す ----
    const auto mp = input.GetMousePos();
    const Vector2 mouse = { mp.x, mp.y };
    const bool click = input.GetMouseTrigger(0);
    if ((mouse - m_LastMouse).LengthSquared() > 0.25f || click)
    {
        m_HoverRow = -1;
        m_HoverButton = -1;
        for (int i = 0; i < n; ++i)
            if (Inside(mouse, RowPos(i), { m_ListSize.x - m_ListPad * 2.0f, m_RowH })) m_HoverRow = i;
        for (int i = 0; i < kButtonCount; ++i)
            if (Inside(mouse, m_Buttons[i].pos, m_Buttons[i].size)) m_HoverButton = i;
        if (m_HoverRow >= 0 && m_HoverRow != m_Cursor)
        {
            m_Cursor = m_HoverRow;
            AudioSystem::Get().Play("ui_hover");
        }
    }
    m_LastMouse = mouse;

    if (click && m_HoverButton >= 0)
    {
        if (m_HoverButton == kBuy) TryBuy();
        else if (m_HoverButton == kReroll) TryReroll();
        else Leave();
        return;
    }

    // ---- キー / パッド ----
    if (input.GetKeyTrigger(VK_RETURN) || input.GetKeyTrigger(VK_SPACE) || input.GetPadTrigger(XINPUT_GAMEPAD_A))
        TryBuy();
    else if (input.GetKeyTrigger('R') || input.GetPadTrigger(XINPUT_GAMEPAD_X))
        TryReroll();
    else if (input.GetKeyTrigger(VK_BACK) || input.GetKeyTrigger(VK_ESCAPE) || input.GetPadTrigger(XINPUT_GAMEPAD_B))
        Leave();
}

// ============================================================
// Render
// ============================================================
void ShopScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);
    auto& gfx = Application::Get().GetGraphics();
    const float k = m_K;

    // ---- 背景：リザルトと同じ写真。左を暗くして一覧を載せる ----
    {
        SceneSnapshot::Backdrop b;
        b.photoCenter = { 0.72f, 0.5f };
        b.zoom = 0.70f;
        b.brightness = 0.90f;
        b.shadeStrength = 0.85f;
        b.shadeFrom = 0.30f;
        b.shadeTo = 0.78f;
        b.vignette = 0.45f;
        b.bottomShade = 0.45f;
        SceneSnapshot::Get().DrawBackdrop(gfx.GetContext(), gfx, b);
    }

    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    const float x0 = m_ScreenW * 0.06f;

    m_Sprite.Begin();
    m_Text.Begin();

    // ---- 見出し：商店 + 百合紋 + 次の面 ----
    {
        const std::wstring header = L"商店";
        const float hs = 1.1f * k;
        const Vector2 size = m_Text.Measure(header, hs);
        const Vector2 pos = { x0, m_ScreenH * 0.06f };
        DrawOutlined(m_Text, header, pos, { 1.0f, 0.86f, 0.52f, 1.0f }, hs, 3.0f * k);

        float y = pos.y + size.y + 2.0f * k;
        const float lineW = (std::max)(size.x, 300.0f * k);
        UIDeco::DrawDivider(m_Sprite, true, { x0 + lineW * 0.5f, y + lineW * 0.05f }, lineW, gold);
        y += lineW * 0.10f + 4.0f * k;

        const int stage = std::clamp(g_RunCarry.stage, 1, StageConfig::kStageCount);
        wchar_t buf[96];
        swprintf_s(buf, L"第%dステージ　%ls へ向かう前に", stage, StageConfig::Get(stage).name);
        DrawOutlined(m_Text, buf, { x0, y }, { 0.86f, 0.84f, 0.80f, 1.0f }, 0.42f * k, 1.5f * k);
    }

    DrawGold();
    DrawRows();

    // ---- 右：選んでいる品のカード ----
    if (m_Stock.Count() > 0)
    {
        const ShopStock::Slot& s = m_Stock.At(m_Cursor);
        const Vector2 cs = m_Cards.CardSize();
        m_Cards.DrawCard(m_Sprite, m_Text, s.id, m_CardPos, !s.sold);
        if (s.sold && m_White)
        {
            m_Sprite.Draw(m_White, m_CardPos, cs, { 0.0f, 0.0f, 0.0f, 0.70f });
            const std::wstring t = L"売り切れ";
            const float ts = 0.9f * k;
            const Vector2 sz = m_Text.Measure(t, ts);
            DrawOutlined(m_Text, t, { m_CardPos.x + (cs.x - sz.x) * 0.5f, m_CardPos.y + (cs.y - sz.y) * 0.5f },
                { 0.80f, 0.76f, 0.70f, 1.0f }, ts, 2.0f * k);
        }
        // カードの下：値段と持っている数
        {
            wchar_t buf[96];
            const int owned = ItemDatabase::GetStat(s.id) ? -1 : g_RunCarry.spellbook.GetCount(s.id);
            if (owned >= 0) swprintf_s(buf, L"金貨 %d　　魔法書に %d 個", s.price, owned);
            else            swprintf_s(buf, L"金貨 %d　　その場で効く", s.price);
            const float ts = 0.38f * k;
            const Vector2 sz = m_Text.Measure(buf, ts);
            DrawOutlined(m_Text, buf, { m_CardPos.x + (cs.x - sz.x) * 0.5f, m_CardPos.y + cs.y + 8.0f * k },
                { gold.x, gold.y, gold.z, 1.0f }, ts, 1.5f * k);
        }
    }

    DrawButtons();

    // ---- 一言（買った / 足りない）----
    if (m_MsgTime > 0.0f && !m_Msg.empty())
    {
        const float a = Clamp01(m_MsgTime / 0.4f);
        const float ts = 0.40f * k;
        const Vector2 pos = { m_ListPos.x, m_Buttons[0].pos.y + m_Buttons[0].size.y + 12.0f * k };
        const Vector4 col = m_MsgGood ? Vector4(1.0f, 0.92f, 0.70f, a) : Vector4(0.95f, 0.30f, 0.22f, a);
        DrawOutlined(m_Text, m_Msg, pos, col, ts, 1.5f * k);
    }

    if (m_Leaving)
    {
        const float blink = 0.6f + 0.4f * std::sin(m_Time * 4.0f);
        DrawOutlined(m_Text, L"読み込み中...", { m_ListPos.x, m_Buttons[0].pos.y + m_Buttons[0].size.y + 12.0f * k },
            { 1.0f, 0.92f, 0.70f, blink }, 0.5f * k, 1.5f * k);
    }

    // ---- 黒から出す ----
    const float black = 1.0f - Smooth(0.0f, kFadeIn, m_Time);
    if (black > 0.0f && m_White)
        m_Sprite.Draw(m_White, { 0.0f, 0.0f }, { m_ScreenW, m_ScreenH }, { 0.0f, 0.0f, 0.0f, black });

    m_Sprite.End();
    m_Text.End();
}

// ============================================================
// 右上：所持金貨（減った瞬間に光る）
// ============================================================
void ShopScene::DrawGold()
{
    const float k = m_K;
    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    const std::wstring num = WithCommas(g_RunCarry.wallet.Coins());
    const float ns = 0.78f * k;
    const float ls = 0.40f * k;
    const Vector2 nsz = m_Text.Measure(num, ns);
    const Vector2 lsz = m_Text.Measure(L"金貨", ls);
    const float icon = 36.0f * k;
    const float right = m_ScreenW * 0.94f;
    const float y = m_ScreenH * 0.07f;

    float x = right - nsz.x;
    const float f = m_GoldFlash;
    const Vector4 numCol = { 1.0f, 0.97f - 0.1f * f, 0.90f - 0.3f * f, 1.0f };
    DrawOutlined(m_Text, num, { x, y }, numCol, ns, 2.0f * k);
    x -= lsz.x + 10.0f * k;
    DrawOutlined(m_Text, L"金貨", { x, y + nsz.y - lsz.y - 4.0f * k }, { gold.x, gold.y, gold.z, 1.0f }, ls, 1.5f * k);
    if (m_GoldIcon)
    {
        x -= icon + 10.0f * k;
        m_Sprite.Draw(m_GoldIcon, { x, y + (nsz.y - icon) * 0.5f }, { icon, icon }, { 1.0f, 0.90f, 0.55f, 1.0f });
    }
}

// ============================================================
// 左：品揃えの一覧
// ============================================================
void ShopScene::DrawRows()
{
    const float k = m_K;
    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);

    UIDeco::PanelStyle ps;
    ps.fill = { 0.0025f, 0.0020f, 0.0045f, 0.92f };
    ps.cornerSize = 24.0f * k;
    ps.innerAlpha = 0.35f;
    UIDeco::DrawPanel(m_Sprite, m_ListPos, m_ListSize, gold, ps);

    const int n = m_Stock.Count();
    const Vector2 rowSize = { m_ListSize.x - m_ListPad * 2.0f, m_RowH };
    const int coins = g_RunCarry.wallet.Coins();
    const float spin = UIDeco::Clock() * 0.12f;
    for (int i = 0; i < n; ++i)
    {
        const ShopStock::Slot& s = m_Stock.At(i);
        const ItemCommon* c = ItemDatabase::GetCommon(s.id);
        if (!c) continue;
        const bool selected = (i == m_Cursor);
        const Vector2 pos = RowPos(i);
        const Vector4 tint = UIDeco::CategoryColor(c->category);
        const float dim = s.sold ? 0.38f : 1.0f;

        // 行の地：選んでいる行は種類の色で光る、他は薄い線
        if (selected)
        {
            UIDeco::PanelStyle rs;
            rs.fill = { 0.0060f, 0.0048f, 0.0100f, 0.95f };
            rs.innerInset = 3.0f * k;
            rs.innerAlpha = 0.45f;
            UIDeco::DrawPanel(m_Sprite, pos, rowSize, tint, rs, 0.8f);
        }
        else
        {
            Vector4 line = tint;
            line.w = 0.22f * dim;
            UIDeco::DrawFrameLines(m_Sprite, pos, rowSize, line);
        }

        // 左：魔法陣の中のアイコン
        const float d = m_RowH * 0.82f;
        const Vector2 center = { pos.x + 8.0f * k + d * 0.5f, pos.y + m_RowH * 0.5f };
        Vector4 ring = tint;
        ring.w = (selected ? 0.75f : 0.40f) * dim;
        UIDeco::DrawCircle(m_Sprite, true, center, d, ring, selected ? spin * 2.0f : spin);
        if (auto icon = GetIcon(s.id))
        {
            const float sz = d * 0.58f;
            m_Sprite.Draw(icon, { center.x - sz * 0.5f, center.y - sz * 0.5f }, { sz, sz }, { 0.97f * dim, 0.94f * dim, 0.87f * dim, 1.0f });
        }

        // 名前 + 種別（2 行）
        const ItemInfo::Sheet sheet = ItemInfo::Describe(s.id);
        const float nameScale = 0.42f * k;
        const float catScale = 0.28f * k;
        const float nameH = m_Text.GetLineHeight(nameScale);
        const float catH = m_Text.GetLineHeight(catScale);
        const float textX = center.x + d * 0.5f + 10.0f * k;
        float ty = pos.y + (m_RowH - nameH - catH + 2.0f * k) * 0.5f;
        m_Text.Draw(sheet.title, { textX, ty }, { 0.96f * dim, 0.92f * dim, 0.84f * dim, 1.0f }, nameScale);
        ty += nameH - 2.0f * k;
        m_Text.Draw(sheet.category, { textX, ty }, { tint.x * dim, tint.y * dim, tint.z * dim, 1.0f }, catScale);

        // 右：値段 / 売り切れ
        const float priceScale = 0.42f * k;
        std::wstring price;
        Vector4 pc;
        if (s.sold)
        {
            price = L"売り切れ";
            pc = { 0.55f, 0.52f, 0.48f, 1.0f };
        }
        else
        {
            price = L"金貨 " + std::to_wstring(s.price);
            pc = (coins >= s.price) ? Vector4(gold.x, gold.y, gold.z, 1.0f) : Vector4(0.80f, 0.22f, 0.16f, 1.0f);
        }
        const Vector2 psz = m_Text.Measure(price, priceScale);
        m_Text.Draw(price, { pos.x + rowSize.x - 12.0f * k - psz.x, pos.y + (m_RowH - psz.y) * 0.5f }, pc, priceScale);
    }
}

// ============================================================
// 一覧の下のボタン
// ============================================================
void ShopScene::DrawButtons()
{
    const float k = m_K;
    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    const float labelScale = 0.44f * k;
    const float keyScale = 0.30f * k;
    const Vector2 pad = { 14.0f * k, 7.0f * k };
    const int coins = g_RunCarry.wallet.Coins();
    for (int i = 0; i < kButtonCount; ++i)
    {
        const Button& b = m_Buttons[i];
        const bool hot = (i == m_HoverButton);
        bool afford = true;
        if (i == kBuy && m_Stock.Count() > 0)
        {
            const ShopStock::Slot& s = m_Stock.At(m_Cursor);
            afford = !s.sold && coins >= s.price;
        }
        if (i == kReroll) afford = coins >= m_Stock.RerollCost();
        const float pulse = hot ? 0.5f + 0.5f * std::sin(m_Time * 3.0f) : 0.0f;

        UIDeco::PanelStyle ps;
        ps.fill = { 0.004f, 0.003f, 0.007f, 0.92f };
        ps.innerInset = 3.0f;
        ps.innerAlpha = hot ? 0.45f : 0.20f;
        Vector4 line = gold;
        line.w = hot ? 1.0f : 0.55f;
        UIDeco::DrawPanel(m_Sprite, b.pos, b.size, line, ps, hot ? 0.6f + 0.4f * pulse : 0.0f);

        const Vector2 ks = InputGlyph::Measure(m_Text, b.key, keyScale);
        const Vector2 ls = m_Text.Measure(b.label, labelScale);
        const float cy = b.pos.y + b.size.y * 0.5f;
        InputGlyph::Draw(m_Sprite, m_Text, b.key, { b.pos.x + pad.x, cy - ks.y * 0.5f }, { gold.x, gold.y, gold.z, 0.9f }, keyScale);
        Vector4 col = hot ? Vector4(1.0f, 0.95f, 0.82f, 1.0f) : Vector4(0.78f, 0.74f, 0.66f, 1.0f);
        if (!afford) col = { 0.60f, 0.10f, 0.07f, 1.0f };   // 線形の暗い赤（四択の引き直しと同じ）
        m_Text.Draw(b.label, { b.pos.x + pad.x + ks.x + 12.0f * k, cy - ls.y * 0.5f }, col, labelScale);
    }
}

// ============================================================
// TEMP-TEST: 自動テスト（VFXL_SHOP_AUTOTEST）
// ============================================================
void ShopScene::TestLog(const char* what)
{
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream f("autotest.log", std::ios::app);
    f << ms << " " << what << " (shop " << m_Time << " s)\n";
}

void ShopScene::AutoTest()
{
    auto stockLine = [&](const char* head)
        {
            std::string s = head;
            for (int i = 0; i < m_Stock.Count(); ++i)
            {
                const ShopStock::Slot& sl = m_Stock.At(i);
                const ItemCommon* c = ItemDatabase::GetCommon(sl.id);
                s += " [" + std::to_string(i) + "]=" + (c ? c->name : "?") + ":" + std::to_string(sl.price) + (sl.sold ? ":sold" : "");
            }
            s += " gold " + std::to_string(g_RunCarry.wallet.Coins());
            TestLog(s.c_str());
        };
    auto buyLine = [&](const char* head)
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "%s slot %d -> gold %d sold %d", head, m_Cursor, g_RunCarry.wallet.Coins(),
            (m_Stock.Count() > m_Cursor) ? (int)m_Stock.At(m_Cursor).sold : -1);
        TestLog(buf);
    };
    const float t = m_Time;
    switch (m_AutoStep)
    {
    case 0: if (t > 0.8f) { stockLine("shop stock"); TestLog("shop look stock"); ++m_AutoStep; } break;
    case 1: if (t > 1.6f) { m_Cursor = 0; TryBuy(); buyLine("shop buy"); ++m_AutoStep; } break;
    case 2: if (t > 2.4f) { m_Cursor = 4; TryBuy(); buyLine("shop buy frame"); ++m_AutoStep; } break;
    case 3: if (t > 3.2f) { TestLog("shop look bought"); ++m_AutoStep; } break;
    case 4: if (t > 3.8f)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "shop reroll cost %d", m_Stock.RerollCost());
        TestLog(buf);
        TryReroll();
        stockLine("shop stock after reroll");
        ++m_AutoStep;
    } break;
    case 5: if (t > 4.6f) { m_Cursor = 0; TryBuy(); buyLine("shop buy again"); ++m_AutoStep; } break;
    case 6: if (t > 5.2f) { TestLog("shop look deny"); ++m_AutoStep; } break;
    case 7: if (t > 6.0f)
    {
        char buf[200];
        int books = 0;
        for (const auto& e : g_RunCarry.spellbook.entries) books += e.count;
        snprintf(buf, sizeof(buf), "shop carry stage %d book %d gold %d hp %.0f mp %.0f regen %.1f shield %.0f",
            g_RunCarry.stage, books, g_RunCarry.wallet.Coins(), g_RunCarry.health.max, g_RunCarry.mana.max,
            g_RunCarry.mana.regen, g_RunCarry.shield.max);
        TestLog(buf);
        TestLog("shop done");
        ++m_AutoStep;
        Leave();
    } break;
    default: break;
    }
}
