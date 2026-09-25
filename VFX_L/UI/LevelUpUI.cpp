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
#include "Manager/ResourceManager.h"
#include "Manager/InputManager.h"
#include "imgui.h"
#include <algorithm>
#include <cwchar>
#include <string>

using namespace DirectX::SimpleMath;

namespace
{
    // 道具の色を白へ寄せる（暗い色でも文字として読めるように）
    Vector4 Brighten(Vector4 c, float t)
    {
        c.x += (1.0f - c.x) * t;
        c.y += (1.0f - c.y) * t;
        c.z += (1.0f - c.z) * t;
        c.w = 1.0f;
        return c;
    }
}

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
    m_CardH = m_CardW * cardAspect;
    m_CardGap = shortSide * cardGapRatio;
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

    for (int i = 0; i < total; ++i)
    {
        const ItemID id = lv.pendingChoices[i];
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) continue;

        const Vector2 pos = CardPosition(i, total);
        const bool selected = (i == m_Cursor);

        // ---- カードの縁（選択中は太く光らせる）----
        const float edge = selected ? m_CardW * 0.035f : m_CardW * 0.015f;
        Vector4 edgeCol = c->color;
        edgeCol.w = selected ? 1.0f : 0.55f;

        sprite.Draw(m_BlockTex,
            { pos.x - edge, pos.y - edge },
            { cardSize.x + edge * 2.0f, cardSize.y + edge * 2.0f },
            edgeCol);

        // ---- カード本体 ----
        sprite.Draw(flat, pos, cardSize,
            selected ? hoverColor : cardColor);

        // ---- 中身：種別 → 名前 → 形（能力値は「+20」）→ 説明・特性・能力値 ----
        // 文字の大きさはカード幅に比例させる（基準は幅 270px の時の ItemSheetView::Style）
        const ItemInfo::Sheet sheet = ItemInfo::Describe(id);
        const ItemSheetView::Style st = textStyle.Scaled(m_CardW / 270.0f);
        const float pad = m_CardW * 0.07f;
        const float inner = m_CardW - pad * 2.0f;
        float y = pos.y + pad;

        text.Draw(sheet.category, { pos.x + pad, y }, Brighten(c->color, 0.35f), st.smallScale);
        y += text.GetLineHeight(st.smallScale);

        // 名前は 1 行に収まるまで縮める
        float titleScale = st.titleScale * 1.2f;
        const float titleW = text.Measure(sheet.title, titleScale).x;
        if (titleW > inner && titleW > 0.0f) titleScale *= inner / titleW;
        text.Draw(sheet.title, { pos.x + pad, y }, { 1, 1, 1, 1 }, titleScale);
        y += text.GetLineHeight(titleScale) + st.sectionGap;

        // ---- 形のプレビュー（アイコンがあれば左に並べる）----
        const float previewH = m_CardH * 0.22f;
        Vector2 areaPos = { pos.x + pad, y };
        Vector2 areaSize = { inner, previewH };

        if (auto icon = GetIcon(id))
        {
            const float s = previewH * 0.9f;
            sprite.Draw(icon, { areaPos.x, y + (previewH - s) * 0.5f }, { s, s });
            areaPos.x += s + pad * 0.5f;
            areaSize.x -= s + pad * 0.5f;
        }

        if (const StatItemDef* stat = ItemDatabase::GetStat(id))
        {
            // 能力値のカード：色の四角に「+20」
            const float sq = previewH * 0.9f;
            const Vector2 sqPos = { areaPos.x + (areaSize.x - sq) * 0.5f, y + (previewH - sq) * 0.5f };
            sprite.Draw(m_BlockTex, sqPos, { sq, sq }, c->color);

            wchar_t amount[16];
            swprintf_s(amount, L"+%d", (int)stat->amount);
            const Vector2 a1 = text.Measure(amount, 1.0f);
            const float s1 = (a1.x > 0.0f) ? (std::min)(1.2f, sq * 0.7f / a1.x) : 1.0f;
            const Vector2 a = text.Measure(amount, s1);
            text.Draw(amount, { sqPos.x + (sq - a.x) * 0.5f, sqPos.y + (sq - a.y) * 0.5f },
                { 1, 1, 1, 1 }, s1);
        }
        else
        {
            DrawShapePreview(sprite, *c, areaPos, areaSize);
        }
        y += previewH + st.sectionGap;

        // ---- 説明・特性・能力値（入り切らなければ縮める）----
        const float avail = pos.y + m_CardH - pad - y;
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

    // 占位格（隙間も塗って 1 枚に）
    ShapeSprite::DrawConnected(sprite, m_BlockTex, c.color, c.occupyCells,
        miniOrigin, miniCell, miniGap);

    // 影響格（薄く。範囲なので 1 マスずつ）
    Vector4 inflCol = c.color;
    inflCol.w = 0.30f;
    for (const auto& off : c.influenceCells)
    {
        const Vector2 cp = {
            miniOrigin.x + off.col * miniPitch,
            miniOrigin.y + off.row * miniPitch
        };
        sprite.Draw(m_BlockTex, cp, { miniCell, miniCell }, inflCol);
    }
}
