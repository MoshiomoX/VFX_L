// ============================================================
// LevelUpUI.cpp
// ============================================================
#include "UI/LevelUpUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Player/LevelComponent.h"
#include "Item/ItemDatabase.h"
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

    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c || !c->iconPath) continue;

        auto tex = ResourceManager::Get().LoadTexture(c->iconPath);
        if (tex) m_Icons.push_back({ id, tex });
    }
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
void LevelUpUI::Draw(SpriteRenderer& sprite, const LevelComponent& lv)
{
    if (!m_BlockTex) return;

    const int total = (int)lv.pendingChoices.size();
    if (total <= 0) return;

    // ---- 背景を暗くする ----
    // 戦闘画面が明るいままだとカードが読めない。
    sprite.Draw(m_BlockTex, { 0.0f, 0.0f }, m_ScreenSize, dimColor);

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
        sprite.Draw(m_BlockTex, pos, cardSize,
            selected ? hoverColor : cardColor);

        // ---- アイコン（無ければ色の四角）----
        const float iconSize = m_CardW * 0.55f;
        const Vector2 iconPos = {
            pos.x + (m_CardW - iconSize) * 0.5f,
            pos.y + m_CardH * 0.12f
        };

        auto icon = GetIcon(id);
        sprite.Draw(icon ? icon : m_BlockTex, iconPos, { iconSize, iconSize },
            icon ? Vector4(1, 1, 1, 1) : c->color);

        // ---- 形状のプレビュー ----
        // どんな形のブロックが手に入るのかを、その場で見せる。
        // 形そのものが性能なので、名前だけでは判断できない。
        const float miniCell = m_CardW * 0.10f;
        const float miniGap = miniCell * 0.12f;
        const float miniPitch = miniCell + miniGap;

        const Vector2 miniOrigin = {
            pos.x + m_CardW * 0.5f,
            pos.y + m_CardH * 0.74f
        };

        // 占位格
        for (const auto& off : c->occupyCells)
        {
            const Vector2 cp = {
                miniOrigin.x + off.col * miniPitch - miniCell * 0.5f,
                miniOrigin.y + off.row * miniPitch - miniCell * 0.5f
            };
            sprite.Draw(m_BlockTex, cp, { miniCell, miniCell }, c->color);
        }

        // 影響格（薄く）
        Vector4 inflCol = c->color;
        inflCol.w = 0.30f;
        for (const auto& off : c->influenceCells)
        {
            const Vector2 cp = {
                miniOrigin.x + off.col * miniPitch - miniCell * 0.5f,
                miniOrigin.y + off.row * miniPitch - miniCell * 0.5f
            };
            sprite.Draw(m_BlockTex, cp, { miniCell, miniCell }, inflCol);
        }
    }
}