// ============================================================
// UIDeco.cpp
// ============================================================
#include "UI/UIDeco.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace DirectX::SimpleMath;

namespace UIDeco
{
    // 戦闘の UI はシーンの HDR バッファに描かれ、合成でトーンマップ + ガンマを通る
    // （0.07 が中間の灰色に見えるのはこのため）。色は sRGB の見た目で決めて、ここで線形へ直す
    static Vector4 FromSrgb(float r, float g, float b)
    {
        return { std::pow(r, 2.2f), std::pow(g, 2.2f), std::pow(b, 2.2f), 1.0f };
    }

    Vector4 TintColor(Tint t)
    {
        switch (t)
        {
        case Tint::Silver: return FromSrgb(0.80f, 0.84f, 0.89f);   // 月銀 #CBD5E3
        case Tint::Arcane: return FromSrgb(0.44f, 0.88f, 0.85f);   // 奥術青 #6FE0DA
        case Tint::Violet: return FromSrgb(0.78f, 0.62f, 0.96f);   // 菫 #C79EF5（召喚物）
        default:           return FromSrgb(0.85f, 0.71f, 0.42f);   // 古金 #D8B46A
        }
    }

    Tint TintFor(ItemCategory c)
    {
        switch (c)
        {
        case ItemCategory::Function: return Tint::Arcane;
        case ItemCategory::Summon:   return Tint::Violet;
        case ItemCategory::Frame:
        case ItemCategory::Stat:     return Tint::Silver;
        default:                     return Tint::Gold;   // 攻撃魔法・範囲魔法・不明
        }
    }

    // 白い円（塗り / 線）を 96px で作る。縁は 1px ぶん滑らかに（mipmap が無いので表示の大きさに近くしておく）
    static std::shared_ptr<Texture> MakeRound(ID3D11Device* device, bool ringOnly)
    {
        constexpr int N = 96;
        const float c = (N - 1) * 0.5f;
        const float outer = N * 0.5f - 1.0f;
        const float lineW = 2.0f;
        std::vector<uint8_t> px(N * N * 4, 255);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
            {
                const float d = std::sqrt((x - c) * (x - c) + (y - c) * (y - c));
                float a = std::clamp(outer - d + 0.5f, 0.0f, 1.0f);                 // 外の縁
                if (ringOnly) a *= std::clamp(d - (outer - lineW) + 0.5f, 0.0f, 1.0f);   // 内の縁
                px[(y * N + x) * 4 + 3] = (uint8_t)(a * 255.0f + 0.5f);
            }
        auto tex = std::make_shared<Texture>();
        if (!tex->CreateFromMemory(device, px.data(), N, N)) return nullptr;
        return tex;
    }

    const Textures& Tex()
    {
        static Textures t;
        static bool loaded = false;
        if (!loaded)
        {
            loaded = true;
            auto& rm = ResourceManager::Get();
            t.white = std::make_shared<Texture>();
            if (!rm.GetDevice() || !t.white->CreateSolid(rm.GetDevice(), 255, 255, 255, 255))
                t.white.reset();
            t.circleStar = rm.LoadTexture(Res::Deco::MagicCircleStar);
            t.circleFlower = rm.LoadTexture(Res::Deco::MagicCircleFlower);
            t.corner = rm.LoadTexture(Res::Deco::CornerKnot);
            t.dividerFleur = rm.LoadTexture(Res::Deco::DividerFleur);
            t.dividerThin = rm.LoadTexture(Res::Deco::DividerThin);
            if (rm.GetDevice())
            {
                t.disc = MakeRound(rm.GetDevice(), false);
                t.ring = MakeRound(rm.GetDevice(), true);
            }
        }
        return t;
    }

    float Clock()
    {
        static const auto start = std::chrono::steady_clock::now();
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
    }

    void DrawFrameLines(SpriteRenderer& sprite, const Vector2& pos, const Vector2& size,
        const Vector4& color, float th)
    {
        const auto& w = Tex().white;
        if (!w) return;
        sprite.Draw(w, pos, { size.x, th }, color);                                    // 上
        sprite.Draw(w, { pos.x, pos.y + size.y - th }, { size.x, th }, color);         // 下
        sprite.Draw(w, { pos.x, pos.y + th }, { th, size.y - th * 2.0f }, color);      // 左
        sprite.Draw(w, { pos.x + size.x - th, pos.y + th }, { th, size.y - th * 2.0f }, color);   // 右
    }

    void DrawPanel(SpriteRenderer& sprite, const Vector2& pos, const Vector2& size,
        const Vector4& tint, const PanelStyle& st, float highlight)
    {
        const Textures& t = Tex();
        if (!t.white) return;

        // ---- 外の光（選択中）: 少しずつ広げた枠を薄く重ねる ----
        if (highlight > 0.0f)
        {
            for (int i = 1; i <= 4; ++i)
            {
                const float g = 2.5f * (float)i;
                Vector4 c = tint;
                c.w = 0.16f * highlight / (float)i;
                DrawFrameLines(sprite, { pos.x - g, pos.y - g }, { size.x + g * 2.0f, size.y + g * 2.0f }, c, 2.5f);
            }
        }

        // ---- 地 ----
        sprite.Draw(t.white, pos, size, st.fill);

        // ---- 二重線（選択中は白に寄せて明るく）----
        Vector4 line = Vector4::Lerp(tint, { 1, 1, 1, 1 }, 0.30f * highlight);
        line.w = 0.80f + 0.20f * highlight;
        DrawFrameLines(sprite, pos, size, line);

        Vector4 inner = tint;
        inner.w = st.innerAlpha * (1.0f + 0.5f * highlight);
        const float k = st.innerInset;
        DrawFrameLines(sprite, { pos.x + k, pos.y + k }, { size.x - k * 2.0f, size.y - k * 2.0f }, inner);

        // ---- 四隅の組紐（右上の絵を反転して使う）----
        if (st.cornerSize > 0.0f && t.corner)
        {
            const float cs = st.cornerSize;
            Vector4 c = tint;
            c.w = st.cornerAlpha;
            const float l = pos.x + k, r = pos.x + size.x - k - cs;
            const float u = pos.y + k, d = pos.y + size.y - k - cs;
            sprite.Draw(t.corner, { r, u }, { cs, cs }, c, { 0.0f, 0.0f, 1.0f, 1.0f });     // 右上
            sprite.Draw(t.corner, { l, u }, { cs, cs }, c, { 1.0f, 0.0f, -1.0f, 1.0f });    // 左上
            sprite.Draw(t.corner, { r, d }, { cs, cs }, c, { 0.0f, 1.0f, 1.0f, -1.0f });    // 右下
            sprite.Draw(t.corner, { l, d }, { cs, cs }, c, { 1.0f, 1.0f, -1.0f, -1.0f });   // 左下
        }
    }

    void DrawDivider(SpriteRenderer& sprite, bool fleur, const Vector2& center, float width, const Vector4& tint)
    {
        const auto& tex = fleur ? Tex().dividerFleur : Tex().dividerThin;
        if (!tex || tex->GetWidth() <= 0) return;
        const float h = width * (float)tex->GetHeight() / (float)tex->GetWidth();
        sprite.Draw(tex, { center.x - width * 0.5f, center.y - h * 0.5f }, { width, h }, tint);
    }

    void DrawCircle(SpriteRenderer& sprite, bool star, const Vector2& center, float diameter,
        const Vector4& tint, float radians)
    {
        const auto& tex = star ? Tex().circleStar : Tex().circleFlower;
        if (!tex) return;
        const float r = diameter * 0.5f;
        // pivot はピクセル座標（SpriteVS がそのまま使う）
        sprite.Draw(tex, { center.x - r, center.y - r }, { diameter, diameter }, tint, radians, center);
    }
}
