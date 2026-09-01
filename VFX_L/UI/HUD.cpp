// ============================================================
// HUD.cpp
// ============================================================
#include "UI/HUD.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Player/LevelComponent.h"
#include <cmath>
#include <cstdio>
#include <iostream>

using namespace DirectX::SimpleMath;

namespace
{
    // 表示用の整数へ（瀕死で 0 と表示しないよう切り上げ）
    int CeilInt(float v) { return (int)std::ceil(v); }

    float Clamp01(float v)
    {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }
}

bool HUD::Initialize(ID3D11Device* device)
{
    m_WhiteTex = std::make_shared<Texture>();
    if (!m_WhiteTex->CreateSolid(device, 255, 255, 255, 255))
    {
        std::cout << "[Error] HUD: white texture failed" << std::endl;
        m_WhiteTex.reset();
        return false;
    }

    std::cout << "[OK] HUD initialized" << std::endl;
    return true;
}

void HUD::Layout(float screenW, float screenH)
{
    m_ScreenW = screenW;
    m_ScreenH = screenH;

    // 経験値バーは最上段の通し。他は左上の固定位置
    m_ExpBarPos = { 0.0f, 0.0f };
    m_ExpBarSize = { screenW, 10.0f };
    m_LvTextPos = { 20.0f, 16.0f };
    m_HpBarPos = { 20.0f, 44.0f };
    m_MpBarPos = { 20.0f, 72.0f };
    m_BarSize = { 260.0f, 22.0f };
}

void HUD::Draw(SpriteRenderer& sprite, TextRenderer& text,
    const HealthComponent& hp, const ManaComponent& mp,
    const LevelComponent& lv)
{
    if (!m_WhiteTex) return;

    // ---- 経験値バー（最上段）----
    // 選択待ちで持ち越し中は 1.0 を超えるので丸める
    DrawBar(sprite, m_ExpBarPos, m_ExpBarSize,
        Clamp01(lv.Progress()), m_ExpColor);

    // ---- HP / MP バー ----
    const float hpRatio = (hp.max > 0.0f) ? hp.current / hp.max : 0.0f;
    const float mpRatio = (mp.max > 0.0f) ? mp.current / mp.max : 0.0f;
    DrawBar(sprite, m_HpBarPos, m_BarSize, Clamp01(hpRatio), m_HpColor);
    DrawBar(sprite, m_MpBarPos, m_BarSize, Clamp01(mpRatio), m_MpColor);

    // ---- 数値（バーの中に重ねる）----
    wchar_t buf[32];

    swprintf_s(buf, L"Lv %d", lv.level);
    text.Draw(buf, m_LvTextPos, m_TextColor, 0.5f);

    swprintf_s(buf, L"HP %d/%d", CeilInt(hp.current), (int)hp.max);
    text.Draw(buf, { m_HpBarPos.x + 8.0f, m_HpBarPos.y + 3.0f },
        m_TextColor, 0.45f);

    swprintf_s(buf, L"MP %d/%d", CeilInt(mp.current), (int)mp.max);
    text.Draw(buf, { m_MpBarPos.x + 8.0f, m_MpBarPos.y + 3.0f },
        m_TextColor, 0.45f);
}

void HUD::DrawBar(SpriteRenderer& sprite,
    const Vector2& pos, const Vector2& size,
    float ratio, const Vector4& fillColor)
{
    // 背景（枠を兼ねる）
    sprite.Draw(m_WhiteTex, pos, size, m_BgColor);

    // 中身は 2px 内側
    const float pad = 2.0f;
    const float w = (size.x - pad * 2.0f) * ratio;
    if (w <= 0.0f) return;

    sprite.Draw(m_WhiteTex,
        { pos.x + pad, pos.y + pad },
        { w, size.y - pad * 2.0f },
        fillColor);
}
