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
#include "UI/InputGlyph.h"
#include "Manager/ResourceManager.h"
#include "Manager/InputManager.h"
#include "imgui.h"

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

    // UI 全体の倍率（2026-10-07）。カードの高さ + 下の引き直しボタンが画面の高さの 9 割を超えるなら収まる所まで
    const float s = UIDeco::FitScale(cardWidthRatio * (cardAspect + 0.30f), 0.90f);
    m_CardW = shortSide * cardWidthRatio * s;
    m_CardGap = shortSide * cardGapRatio * s;
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
void LevelUpUI::RerollRect(int total, Vector2& pos, Vector2& size) const
{
    const float k = m_CardW / 270.0f;
    size = { m_CardW * rerollWidthRatio, 48.0f * k };
    const float cardBottom = CardPosition(0, (std::max)(1, total)).y + m_CardH;
    pos = { (m_ScreenSize.x - size.x) * 0.5f, cardBottom + m_CardW * 0.10f };
}

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
bool LevelUpUI::HandleInput(const LevelComponent& lv, ItemID& outPicked, float dt)
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
        // 実時間で減らす（ゲームは止まっているが GameUI は実時間の dt をくれる）。
        // 以前は 1 フレーム 0.016 秒の決め打ちで、144 fps だと 0.25 秒の猶予が 0.11 秒ほどしか無かった。
        // 1 フレームだけ長い時（読み込みの引っ掛かり）に猶予を食い尽くさないよう 0.1 秒で頭打ち
        m_InputDelay -= (std::min)((std::max)(dt, 0.0f), 0.1f);
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

    // ---- 金貨で引き直す（R / パッド X / ボタンのクリック）。同じフレームには決定しない ----
    m_RerollHover = false;
    if (m_RerollCost >= 0)
    {
        Vector2 bp, bs;
        RerollRect(total, bp, bs);
        m_RerollHover = mouse.x >= bp.x && mouse.x <= bp.x + bs.x && mouse.y >= bp.y && mouse.y <= bp.y + bs.y;
        if (input.GetKeyTrigger('R') || input.GetPadTrigger(XINPUT_GAMEPAD_X) ||
            (input.GetMouseTrigger(0) && m_RerollHover))
        {
            m_RerollRequested = true;
            return false;
        }
    }

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
    if (total <= 0) { m_Shown = false; return; }

    // 出た瞬間（0 → 1）。暗幕は薄い所から、カードは下から浮き上がる
    const float now = UIDeco::Clock();
    if (!m_Shown) { m_Shown = true; m_ShownAt = now; }
    float in = (appearTime > 0.0f) ? (now - m_ShownAt) / appearTime : 1.0f;
    in = in < 0.0f ? 0.0f : (in > 1.0f ? 1.0f : in);
    const float ease = 1.0f - (1.0f - in) * (1.0f - in) * (1.0f - in);

    // ---- 背景を暗くする ----
    // 戦闘画面が明るいままだとカードが読めない。
    const auto& flat = m_WhiteTex ? m_WhiteTex : m_BlockTex;   // 暗幕とカードの地は無地（文字を読みやすく）
    Vector4 dim = dimColor;
    dim.w *= ease;
    sprite.Draw(flat, { 0.0f, 0.0f }, m_ScreenSize, dim);

    const float k = m_CardW / 270.0f;   // 文字や飾りの大きさの基準（カード幅 270px の時 1）
    const float slide = (1.0f - ease) * m_CardH * 0.12f;   // 下からの浮き上がり（px）

    // ---- 見出し「レベルアップ」/「宝箱の報酬」+ 百合紋の分割線（HUD・メニューと同じ古金）。
    //      箱から出た四択は見出しを変える（2026-10-18 ユーザー：箱でも「レベルアップ」と出ていた）----
    {
        const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
        const float s = headingScale * k;
        const std::wstring head = lv.choiceFromCrate ? L"宝箱の報酬" : L"レベルアップ";
        const Vector2 hs = text.Measure(head, s);
        const float cardTop = CardPosition(0, total).y;
        const float divW = m_CardW * 1.25f;
        const float divH = divW * 0.10f;
        const float headY = cardTop - hs.y - divH - m_CardW * 0.10f;
        Vector4 hc = headingColor;
        hc.w *= ease;
        Vector4 gc = gold;
        gc.w *= ease;
        text.Draw(head, { (m_ScreenSize.x - hs.x) * 0.5f, headY }, hc, s);
        UIDeco::DrawDivider(sprite, true, { m_ScreenSize.x * 0.5f, headY + hs.y + divH * 0.45f }, divW, gc);
    }

    for (int i = 0; i < total; ++i)
    {
        const bool selected = (i == m_Cursor);
        Vector2 pos = CardPosition(i, total);
        if (selected) pos.y -= m_CardH * 0.015f;   // 選択中は少し浮かせる
        pos.y += slide;
        DrawCard(sprite, text, lv.pendingChoices[i], pos, selected);
    }

    // ---- 下の案内：Tab / Start でバックパックを見る（四択の上に開く。2026-10-15）----
    {
        const float s = rerollTextScale * 0.85f * k;
        const wchar_t* hint = L"<{K:Tab}|{START}>  バックパックを見る・置く";
        const Vector2 ts = InputGlyph::Measure(text, hint, s);
        Vector2 bp, bs;
        RerollRect(total, bp, bs);
        const float y = (m_RerollCost >= 0) ? bp.y + bs.y + 10.0f * k : bp.y;
        InputGlyph::Draw(sprite, text, hint, { (m_ScreenSize.x - ts.x) * 0.5f, y },
            { 0.78f, 0.74f, 0.66f, 0.9f * ease }, s);
    }

    // ---- 金貨で引き直すボタン（2026-10-04）。足りない時は文字を暗い赤に ----
    if (m_RerollCost >= 0)
    {
        Vector2 bp, bs;
        RerollRect(total, bp, bs);
        const bool afford = m_Gold >= m_RerollCost;
        UIDeco::PanelStyle ps;
        ps.fill = m_RerollHover ? hoverColor : cardColor;
        ps.innerInset = 4.0f * k;
        UIDeco::DrawPanel(sprite, bp, bs, UIDeco::TintColor(UIDeco::Tint::Gold), ps, (m_RerollHover && afford) ? 1.0f : 0.0f);

        wchar_t buf[96];
        swprintf_s(buf, L"<{K:R}|{X}>  引き直す     金貨 %d   （所持 %d）", m_RerollCost, m_Gold);   // <キーボード|パッド> の絵（InputGlyph）
        const float s = rerollTextScale * k;
        const Vector2 ts = InputGlyph::Measure(text, buf, s);
        const Vector4 col = afford ? headingColor : Vector4(0.60f, 0.10f, 0.07f, 1.0f);   // 線形の暗い赤
        InputGlyph::Draw(sprite, text, buf, { bp.x + (bs.x - ts.x) * 0.5f, bp.y + (bs.y - ts.y) * 0.5f }, col, s);
    }
}

// ============================================================
// カード 1 枚（レベルアップの四択と商店で共用。2026-10-15 に Draw から切り出した）
// ============================================================
void LevelUpUI::DrawCard(SpriteRenderer& sprite, TextRenderer& text, ItemID id,
    const Vector2& pos, bool selected) const
{
    if (!m_BlockTex) return;
    const ItemCommon* c = ItemDatabase::GetCommon(id);
    if (!c) return;

    const Vector2 cardSize = CardSize();
    const float k = m_CardW / 270.0f;   // 文字や飾りの大きさの基準（カード幅 270px の時 1）
    const float spin = UIDeco::Clock() * circleSpin;

    {
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
// 占有マス + 影響マスの外接矩形を枠（areaPos, areaSize）の中央に合わせ、
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

    // 占有マス（バックパックと同じ「色ガラス + 種類の色の輪郭」。隙間も塗って 1 枚に）
    const auto& white = UIDeco::Tex().white ? UIDeco::Tex().white : m_BlockTex;
    const Vector4 tint = UIDeco::CategoryColor(c.category);
    ShapeSprite::DrawItemGlass(sprite, white, c.color, tint, nullptr, c.occupyCells,
        miniOrigin, miniCell, miniGap);

    // 影響マス（薄く塗って縁を引く。範囲なので 1 マスずつ）
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
