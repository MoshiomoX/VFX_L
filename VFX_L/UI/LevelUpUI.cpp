// ============================================================
// LevelUpUI.cpp
// ============================================================
#include "UI/LevelUpUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Player/LevelComponent.h"
#include "Item/ItemDatabase.h"
#include "Item/ItemInfo.h"
#include "UI/ShapeSprite.h"
#include "UI/UIDeco.h"
#include "Manager/ResourceManager.h"
#include "Manager/InputManager.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

using namespace DirectX::SimpleMath;

void LevelUpUI::Initialize(std::shared_ptr<Texture> blockTex)
{
    m_BlockTex = blockTex;
}

void LevelUpUI::LoadIcons()
{
    m_Icons.clear();

    // 能力値（生命・魔力の上限）も三択に出るので、両方の一覧から読む
    auto load = [&](const std::vector<ItemID>& ids)
        {
            for (ItemID id : ids)
            {
                const ItemCommon* c = ItemDatabase::GetCommon(id);
                if (!c || !c->iconPath) continue;

                auto tex = ResourceManager::Get().LoadTexture(c->iconPath);
                if (tex) m_Icons.push_back({ id, tex });
            }
        };
    load(ItemDatabase::GetAllIDs());
    load(ItemDatabase::GetLevelUpOnlyIDs());
}

std::shared_ptr<Texture> LevelUpUI::GetIcon(ItemID id) const
{
    for (const auto& p : m_Icons)
        if (p.first == id) return p.second;
    return nullptr;
}

void LevelUpUI::Layout(float screenW, float screenH)
{
    m_ScreenSize = { screenW, screenH };

    const float shortSide = (screenW < screenH) ? screenW : screenH;

    m_CardW = shortSide * cardWidthRatio;
    m_CardGap = shortSide * cardGapRatio;
    // 候補 fitCards 枚が画面幅の 94% に収まらなければ細くする（縦長・狭い窓でも 4 枚並ぶように）
    const float n = (float)(fitCards < 1 ? 1 : fitCards);
    const float maxW = (screenW * 0.94f - m_CardGap * (n - 1.0f)) / n;
    if (m_CardW > maxW) m_CardW = maxW;
    m_CardH = m_CardW * cardAspect;
}

Vector2 LevelUpUI::CardSize() const
{
    return { m_CardW, m_CardH };
}

// ============================================================
// カードの左上座標
// 枚数に応じて全体を中央寄せする
// ============================================================
Vector2 LevelUpUI::CardPosition(int index, int total) const
{
    if (total <= 0) return { 0.0f, 0.0f };

    const float totalW = m_CardW * (float)total + m_CardGap * (float)(total - 1);
    const float startX = (m_ScreenSize.x - totalW) * 0.5f;

    return {
        startX + (m_CardW + m_CardGap) * (float)index,
        m_ScreenSize.y * centerY - m_CardH * 0.5f
    };
}

// ============================================================
// 入力
//
// マウスで直接クリック、または左右キー + 決定。
// どちらでも選べるようにする。
// ============================================================
bool LevelUpUI::HandleInput(const LevelComponent& lv, ItemID& outPicked)
{
    const int total = (int)lv.pendingChoices.size();
    if (total <= 0)
    {
        m_WasChoosing = false;
        m_Hover = -1;
        return false;
    }

    auto& input = InputManager::Get();

    // 表示された最初のフレームで初期化する。
    // 押しっぱなしの入力で即決定されるのを防ぐため猶予を置く。
    if (!m_WasChoosing)
    {
        m_WasChoosing = true;
        m_Cursor = 0;
        m_Hover = -1;
        m_InputDelay = 0.25f;
    }

    if (m_InputDelay > 0.0f)
    {
        // 一時停止中なので dt が無い。固定値で減らす。
        // 厳密である必要は無く、数フレーム待てればよい。
        m_InputDelay -= 0.016f;
        return false;
    }

    if (m_Cursor < 0) m_Cursor = 0;
    if (m_Cursor >= total) m_Cursor = total - 1;

    // ---- マウス位置からカードを判定 ----
    const auto mp = input.GetMousePos();
    const Vector2 mouse = { mp.x, mp.y };

    m_Hover = -1;
    for (int i = 0; i < total; ++i)
    {
        const Vector2 pos = CardPosition(i, total);
        if (mouse.x >= pos.x && mouse.x <= pos.x + m_CardW &&
            mouse.y >= pos.y && mouse.y <= pos.y + m_CardH)
        {
            m_Hover = i;
            m_Cursor = i;   // マウスを動かしたらカーソルも合わせる
            break;
        }
    }

    // ---- 左右キー / スティックでカーソル移動 ----
    if (input.GetKeyTrigger(VK_LEFT) || input.GetKeyTrigger('A'))
        m_Cursor = (m_Cursor + total - 1) % total;
    if (input.GetKeyTrigger(VK_RIGHT) || input.GetKeyTrigger('D'))
        m_Cursor = (m_Cursor + 1) % total;

    if (input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_LEFT))
        m_Cursor = (m_Cursor + total - 1) % total;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_RIGHT))
        m_Cursor = (m_Cursor + 1) % total;

    // ---- 決定 ----
    bool decided = false;

    // マウスは乗っているカードを直接選ぶ
    if (input.GetMouseTrigger(0) && m_Hover >= 0)
    {
        m_Cursor = m_Hover;
        decided = true;
    }

    if (input.GetKeyTrigger(VK_RETURN) || input.GetKeyTrigger(VK_SPACE))
        decided = true;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_A))
        decided = true;

    if (!decided) return false;

    outPicked = lv.pendingChoices[m_Cursor];
    m_WasChoosing = false;
    return true;
}

// ============================================================
// 描画
// 画面全体を暗くしてから、カードを並べる
// ============================================================
void LevelUpUI::Draw(SpriteRenderer& sprite, TextRenderer& text, const LevelComponent& lv)
{
    if (!m_BlockTex) return;

    const int total = (int)lv.pendingChoices.size();
    if (total <= 0) return;

    // ---- 背景を暗くする ----
    // 戦闘画面が明るいままだとカードが読めない。
    const auto& flat = m_WhiteTex ? m_WhiteTex : m_BlockTex;   // 暗幕とカードの地は無地（文字を読みやすく）
    sprite.Draw(flat, { 0.0f, 0.0f }, m_ScreenSize, dimColor);

    const Vector2 cardSize = CardSize();
    const float k = m_CardW / 270.0f;   // 文字や飾りの大きさの基準（カード幅 270px の時 1）
    const float spin = UIDeco::Clock() * circleSpin;

    // ---- 見出し「レベルアップ」+ 百合紋の分割線（HUD・メニューと同じ古金）----
    {
        const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
        const float s = headingScale * k;
        const std::wstring head = L"レベルアップ";
        const Vector2 hs = text.Measure(head, s);
        const float cardTop = CardPosition(0, total).y;
        const float divW = m_CardW * 1.25f;
        const float divH = divW * 0.10f;
        const float headY = cardTop - hs.y - divH - m_CardW * 0.10f;
        text.Draw(head, { (m_ScreenSize.x - hs.x) * 0.5f, headY }, headingColor, s);
        UIDeco::DrawDivider(sprite, true, { m_ScreenSize.x * 0.5f, headY + hs.y + divH * 0.45f }, divW, gold);
    }

    for (int i = 0; i < total; ++i)
    {
        const ItemID id = lv.pendingChoices[i];
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) continue;

        const bool selected = (i == m_Cursor);
        Vector2 pos = CardPosition(i, total);
        if (selected) pos.y -= m_CardH * 0.015f;   // 選択中は少し浮かせる

        // ---- カード本体：種類の色の二重線 + 四隅の組紐。選択中は光らせる ----
        const Vector4 tint = UIDeco::CategoryColor(c->category);
        UIDeco::PanelStyle ps;
        ps.fill = selected ? hoverColor : cardColor;
        ps.innerInset = 5.0f * k;
        ps.cornerSize = m_CardW * cornerRatio;
        UIDeco::DrawPanel(sprite, pos, cardSize, tint, ps, selected ? 1.0f : 0.0f);

        // ---- 中身：種別 → 名前 → 魔法陣の中のアイコン + 形（能力値は「+20」）→ 分割線 → 説明・特性・能力値 ----
        // 文字の大きさはカード幅に比例させる（基準は幅 270px の時の ItemSheetView::Style）
        const ItemInfo::Sheet sheet = ItemInfo::Describe(id);
        const ItemSheetView::Style st = textStyle.Scaled(k);
        const float pad = m_CardW * 0.08f;
        const float inner = m_CardW - pad * 2.0f;
        const float cx = pos.x + m_CardW * 0.5f;
        float y = pos.y + pad;

        // 種別と名前は中央揃え（四隅の組紐の間に入る）
        const Vector2 catSize = text.Measure(sheet.category, st.smallScale);
        text.Draw(sheet.category, { cx - catSize.x * 0.5f, y }, tint, st.smallScale);
        y += text.GetLineHeight(st.smallScale);

        // 名前は 1 行に収まるまで縮める
        const float titleMax = m_CardW - m_CardW * cornerRatio * 1.4f;
        float titleScale = st.titleScale * 1.3f;
        const float titleW = text.Measure(sheet.title, titleScale).x;
        if (titleW > titleMax && titleW > 0.0f) titleScale *= titleMax / titleW;
        const Vector2 titleSize = text.Measure(sheet.title, titleScale);
        text.Draw(sheet.title, { cx - titleSize.x * 0.5f, y }, headingColor, titleScale);
        y += text.GetLineHeight(titleScale) + st.sectionGap;

        // ---- 左：魔法陣の中のアイコン / 右：形のプレビュー ----
        const float previewH = m_CardH * 0.24f;
        Vector2 areaPos = { pos.x + pad, y };
        Vector2 areaSize = { inner, previewH };
        {
            const float d = previewH;
            const Vector2 center = { areaPos.x + d * 0.5f, y + previewH * 0.5f };
            Vector4 ring = tint;
            ring.w = selected ? 0.75f : 0.50f;
            UIDeco::DrawCircle(sprite, true, center, d, ring, selected ? spin * 2.0f : spin);
            if (auto icon = GetIcon(id))
            {
                const float s = d * 0.56f;
                sprite.Draw(icon, { center.x - s * 0.5f, center.y - s * 0.5f }, { s, s }, iconColor);
            }
            areaPos.x += d + pad * 0.5f;
            areaSize.x -= d + pad * 0.5f;
        }

        if (const StatItemDef* stat = ItemDatabase::GetStat(id))
        {
            // 能力値のカード：右側に大きく「+20」（割合の物は「+8%」）
            wchar_t amount[16];
            if (stat->percent) swprintf_s(amount, L"+%d%%", (int)std::lround(stat->amount * 100.0f));
            else               swprintf_s(amount, L"+%d", (int)stat->amount);
            const Vector2 a1 = text.Measure(amount, 1.0f);
            const float s1 = (a1.x > 0.0f) ? (std::min)(1.4f * k, areaSize.x * 0.7f / a1.x) : 1.0f;
            const Vector2 a = text.Measure(amount, s1);
            text.Draw(amount, { areaPos.x + (areaSize.x - a.x) * 0.5f, y + (previewH - a.y) * 0.5f }, tint, s1);
        }
        else
        {
            DrawShapePreview(sprite, *c, areaPos, areaSize);
        }
        y += previewH + st.sectionGap * 0.5f;

        // ---- 分割線 ----
        {
            const float dw = inner * 0.85f;
            const float dh = dw * 0.05f;
            Vector4 dc = tint;
            dc.w = 0.75f;
            UIDeco::DrawDivider(sprite, false, { cx, y + dh * 0.5f }, dw, dc);
            y += dh + st.sectionGap * 0.5f;
        }

        // ---- 説明・特性・能力値（入り切らなければ縮める）。下の組紐とは重ねない ----
        const float avail = pos.y + m_CardH - (std::max)(pad, m_CardW * cornerRatio * 0.55f) - y;
        const float need = ItemSheetView::DrawBody(nullptr, nullptr, text, sheet, { 0, 0 }, inner, st, false);
        const ItemSheetView::Style body = (need > avail && need > 0.0f)
            ? st.Scaled((std::max)(0.6f, avail / need)) : st;
        ItemSheetView::DrawBody(&sprite, m_WhiteTex ? m_WhiteTex : m_BlockTex, text, sheet, { pos.x + pad, y }, inner, body, true);
    }
}

// ============================================================
// 形のプレビュー
// どんな形のブロックが手に入るのかを、その場で見せる。
// 形そのものが性能なので、名前だけでは判断できない。
// 占位格 + 影響格の外接矩形を枠（areaPos, areaSize）の中央に合わせ、
// 大きい形は収まるまでマスを縮める。
// 異形はアンカーが真ん中とは限らないので、アンカー基準だと片寄る
// ============================================================
void LevelUpUI::DrawShapePreview(SpriteRenderer& sprite, const ItemCommon& c,
    const Vector2& areaPos, const Vector2& areaSize) const
{
    int minR = 0, maxR = 0, minC = 0, maxC = 0;
    bool first = true;
    auto grow = [&](const std::vector<CellOffset>& cells)
        {
            for (const auto& o : cells)
            {
                if (first) { minR = maxR = o.row; minC = maxC = o.col; first = false; continue; }
                minR = (std::min)(minR, o.row); maxR = (std::max)(maxR, o.row);
                minC = (std::min)(minC, o.col); maxC = (std::max)(maxC, o.col);
            }
        };
    grow(c.occupyCells);
    grow(c.influenceCells);
    if (first) return;

    const int spanC = maxC - minC + 1;
    const int spanR = maxR - minR + 1;
    const float gapRatio = 0.12f;
    float miniCell = m_CardW * 0.10f;
    auto extent = [&](int n) { return miniCell * ((float)n + gapRatio * (float)(n - 1)); };
    const float fit = (std::min)({ 1.0f, areaSize.x * 0.9f / extent(spanC), areaSize.y * 0.9f / extent(spanR) });
    miniCell *= fit;

    const float miniGap = miniCell * gapRatio;
    const float miniPitch = miniCell + miniGap;

    // アンカーのマスの左上
    const Vector2 miniOrigin = {
        areaPos.x + areaSize.x * 0.5f - extent(spanC) * 0.5f - (float)minC * miniPitch,
        areaPos.y + areaSize.y * 0.5f - extent(spanR) * 0.5f - (float)minR * miniPitch
    };

    // 占位格（背包と同じ「色ガラス + 種類の色の輪郭」。隙間も塗って 1 枚に）
    const auto& white = UIDeco::Tex().white ? UIDeco::Tex().white : m_BlockTex;
    const Vector4 tint = UIDeco::CategoryColor(c.category);
    ShapeSprite::DrawItemGlass(sprite, white, c.color, tint, nullptr, c.occupyCells,
        miniOrigin, miniCell, miniGap);

    // 影響格（薄く塗って縁を引く。範囲なので 1 マスずつ）
    Vector4 inflCol = tint;
    inflCol.w = 0.18f;
    Vector4 inflEdge = tint;
    inflEdge.w = 0.70f;
    for (const auto& off : c.influenceCells)
    {
        const Vector2 cp = {
            miniOrigin.x + off.col * miniPitch,
            miniOrigin.y + off.row * miniPitch
        };
        sprite.Draw(white, cp, { miniCell, miniCell }, inflCol);
        UIDeco::DrawFrameLines(sprite, cp, { miniCell, miniCell }, inflEdge);
    }
}
