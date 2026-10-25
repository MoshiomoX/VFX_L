// ============================================================
// HowToPlayUI.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "UI/HowToPlayUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Manager/InputManager.h"
#include "Audio/AudioSystem.h"
#include "UI/UIDeco.h"
#include "UI/InputGlyph.h"

#include <Xinput.h>

using namespace DirectX::SimpleMath;

namespace
{
    // 開いた直後の猶予・押しっぱなしの連続は実時間で減らす（以前は 1 フレーム 0.016 秒の決め打ちで、
    // 144 fps だと猶予が半分以下・連続が 2 倍以上速かった）。1 フレームだけ長い時（読み込みの引っ掛かり）に
    // 猶予を食い尽くさないよう 0.1 秒で頭打ち
    float FrameStep(float dt) { return (std::min)((std::max)(dt, 0.0f), 0.1f); }
    constexpr float kPadRatio = 0.04f;      // 余白（画面短辺に対する比率）
    constexpr float kTitleRatio = 0.09f;    // 見出しの高さ
    constexpr float kBodyRatio = 0.40f;     // 本文の高さ（6 行 + 題）
    constexpr float kHintRatio = 0.06f;     // 操作の案内
    constexpr float kWidthRatio = 0.86f;

    const Vector4 kTextMain = { 0.96f, 0.92f, 0.84f, 1.0f };
    const Vector4 kTextBody = { 0.86f, 0.84f, 0.80f, 1.0f };
    const Vector4 kTextDim = { 0.60f, 0.60f, 0.65f, 1.0f };
}

HowToPlayUI::HowToPlayUI()
{
    // 文字の中の <キーボード|パッド> は今使っている機器の方だけ出る（InputGlyph）
    m_Pages.push_back({ L"操作", {
        L"・移動　<{K:W} {K:A} {K:S} {K:D}|{LS}>　　カメラ　<マウス|右スティック>",
        L"・ジャンプ　<{K:Space}|{A}>　（カードで空中でのジャンプが増える）",
        L"・スライディング　<{K:Ctrl}|{X}>　坂を下ると加速する。跳ねても勢いは残る",
        L"・魔力解放　<{K:Q}|{Y}>　3 秒間 MP を使わずに撃ち続ける（20 秒に 1 回）",
        L"・バックパック　<{K:Tab}|{START}>　　調べる・開ける　<{K:F}|{B}>",
        L"・一時停止　<{K:Esc}|{BACK}>　　設定（音量・縁取り）もここから",
    } });
    m_Pages.push_back({ L"魔法とバックパック", {
        L"・魔法は近くの敵を自動で狙って撃つ。狙いを付ける必要はない",
        L"・手に入れた魔法やルーンは魔法書に入る。バックパックの枠に置くと撃ち始める",
        L"・ルーンは隣の魔法を強化する。薄く光るマスに魔法が掛かっていれば効く",
        L"・上級魔法は自分では撃たない。決まった基本魔法を隣接させると発動する",
        L"・水晶玉は左右の魔法を貯蔵し、プレイヤーの周りの光球が代わりに撃つ",
        L"・拡張枠のカードで置ける範囲が広がる。置き方を工夫するのがこのゲームの要",
    } });
    m_Pages.push_back({ L"戦い方", {
        L"・敵を倒すと経験玉が落ちる。拾うとレベルアップし、金貨にもなる",
        L"・レベルアップでは 4 枚のカードから 1 枚を選ぶ。金貨で引き直せる",
        L"・宝箱は金貨で開く。開けるたびに値段が上がる。磁石は経験玉を一度に集める",
        L"・シールドは一撃を丸ごと防ぎ、しばらく被弾しなければ回復する",
        L"・10 分経つと最終ウェーブ。壁を抜ける幽霊が押し寄せてくる",
        L"・ボスの門を見つけて呼び出し、倒したら門から次のステージへ（全 3 ステージ）",
    } });
}

void HowToPlayUI::Layout(float screenW, float screenH)
{
    m_Screen = { screenW, screenH };
    m_Short = (std::min)(screenW, screenH);
    const float baseH = kPadRatio * 2.0f + kTitleRatio + kBodyRatio + kHintRatio;
    m_Scale = UIDeco::FitScale(baseH, 0.95f);
    const float s = m_Short * m_Scale;
    m_PanelSize = { (std::min)(s * kWidthRatio * 1.45f, screenW * 0.92f), s * baseH };
    m_PanelPos = { (screenW - m_PanelSize.x) * 0.5f, (screenH - m_PanelSize.y) * 0.5f };
}

void HowToPlayUI::Open()
{
    m_Open = true;
    m_Page = 0;
    m_InputDelay = 0.15f;
}

void HowToPlayUI::Flip(int dir)
{
    const int n = (int)m_Pages.size();
    const int next = m_Page + dir;
    if (next < 0 || next >= n) return;
    m_Page = next;
    AudioSystem::Get().Play("ui_hover");
}

bool HowToPlayUI::HandleInput(float dt)
{
    const float frameDt = FrameStep(dt);
    if (!m_Open) return true;
    auto& input = InputManager::Get();

    if (input.GetPadTrigger(XINPUT_GAMEPAD_B) || input.GetKeyTrigger(VK_ESCAPE))
    {
        m_Open = false;
        AudioSystem::Get().Play("ui_close");
        return true;
    }
    if (m_InputDelay > 0.0f)
    {
        m_InputDelay -= frameDt;
        return false;
    }

    const bool left = input.GetKeyTrigger('A') || input.GetKeyTrigger(VK_LEFT)
        || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_LEFT) || input.GetPadTrigger(XINPUT_GAMEPAD_LEFT_SHOULDER);
    const bool right = input.GetKeyTrigger('D') || input.GetKeyTrigger(VK_RIGHT)
        || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_RIGHT) || input.GetPadTrigger(XINPUT_GAMEPAD_RIGHT_SHOULDER);
    const bool next = input.GetKeyTrigger(VK_RETURN) || input.GetKeyTrigger(VK_SPACE)
        || input.GetPadTrigger(XINPUT_GAMEPAD_A) || input.GetMouseTrigger(0);
    if (left) Flip(-1);
    else if (right) Flip(+1);
    else if (next)
    {
        if (m_Page + 1 < (int)m_Pages.size()) Flip(+1);
        else
        {
            m_Open = false;
            AudioSystem::Get().Play("ui_close");
            return true;
        }
    }
    return false;
}

// ============================================================
// 描画：暗幕 → 箱 → 見出し（遊び方　n / 3）→ ページの題 → 行 → 操作の案内
// ============================================================
void HowToPlayUI::Draw(SpriteRenderer& sprite, TextRenderer& text, const std::shared_ptr<Texture>& white)
{
    if (!white || !m_Open) return;

    sprite.Draw(white, { 0.0f, 0.0f }, m_Screen, dimColor);

    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    {
        UIDeco::PanelStyle ps;
        ps.fill = panelColor;
        ps.innerInset = (std::max)(4.0f, m_Short * 0.007f);
        ps.cornerSize = m_Short * 0.10f;
        UIDeco::DrawPanel(sprite, m_PanelPos, m_PanelSize, gold, ps);
    }

    const float k = m_Short / 900.0f * m_Scale;
    const float s = m_Short * m_Scale;
    const float pad = s * kPadRatio;
    const PageData& page = m_Pages[std::clamp(m_Page, 0, (int)m_Pages.size() - 1)];

    // ---- 見出し + 百合紋の分割線 ----
    wchar_t head[64];
    swprintf_s(head, L"遊び方　%d / %d", m_Page + 1, (int)m_Pages.size());
    const float titleScale = 0.8f * k;
    const Vector2 ts = text.Measure(head, titleScale);
    const float titleH = s * kTitleRatio;
    const float titleY = m_PanelPos.y + pad + (titleH - ts.y) * 0.5f - titleH * 0.2f;
    text.Draw(head, { m_PanelPos.x + (m_PanelSize.x - ts.x) * 0.5f, titleY }, kTextMain, titleScale);
    UIDeco::DrawDivider(sprite, true, { m_PanelPos.x + m_PanelSize.x * 0.5f, titleY + ts.y + titleH * 0.08f },
        m_PanelSize.x * 0.62f, gold);

    // ---- ページの題（金）と行 ----
    float y = m_PanelPos.y + pad + titleH + s * 0.02f;
    const float subScale = 0.56f * k;
    const Vector2 ss = text.Measure(page.title, subScale);
    text.Draw(page.title, { m_PanelPos.x + pad * 1.2f, y }, gold, subScale);
    y += ss.y + s * 0.022f;

    const float lineScale = 0.40f * k;
    const float lineH = text.GetLineHeight(lineScale) * 1.30f;
    const float x = m_PanelPos.x + pad * 1.2f;
    for (const std::wstring& line : page.lines)
    {
        if (line.empty()) { y += lineH * 0.5f; continue; }
        InputGlyph::Draw(sprite, text, line, { x, y }, kTextBody, lineScale);
        y += lineH;
    }

    // ---- ページの点（・○●）と操作の案内 ----
    const float hintScale = 0.34f * k;
    const std::wstring hint = (m_Page + 1 < (int)m_Pages.size())
        ? L"<{K:A} {K:D}|{DPADH}> ページ　　<{K:Enter}|{A}> 次へ　　<{K:Esc}|{B}> 閉じる"
        : L"<{K:A} {K:D}|{DPADH}> ページ　　<{K:Enter}|{A}> 閉じる";
    const Vector2 hs = InputGlyph::Measure(text, hint, hintScale);
    const float hintTop = m_PanelPos.y + m_PanelSize.y - pad - s * kHintRatio;
    InputGlyph::Draw(sprite, text, hint, { m_PanelPos.x + (m_PanelSize.x - hs.x) * 0.5f, hintTop + (s * kHintRatio - hs.y) * 0.5f },
        kTextDim, hintScale);
    {
        const float d = s * 0.012f, gap = d * 2.2f;
        const float cx0 = m_PanelPos.x + m_PanelSize.x * 0.5f - gap * (float)(m_Pages.size() - 1) * 0.5f;
        const float cy = hintTop - s * 0.012f;
        for (int i = 0; i < (int)m_Pages.size(); ++i)
        {
            Vector4 c = gold;
            if (i != m_Page) c.w = 0.35f;
            sprite.Draw(white, { cx0 + gap * (float)i - d * 0.5f, cy - d * 0.5f }, { d, d }, c, 0.785398f, { cx0 + gap * (float)i, cy });
        }
    }
}
