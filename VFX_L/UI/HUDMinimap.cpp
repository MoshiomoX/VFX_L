// ============================================================
// HUDMinimap.cpp
// 右上の丸いミニマップ（UI/HUDMinimap.h）
// ============================================================
#include "UI/HUDMinimap.h"
#include "UI/HUD.h"
#include "UI/UIDeco.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Graphics/Material/Texture.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kPi = 3.14159265f;
    // 目印を円の縁に留める半径（円の半径に対する比）。これより外の物は縁に寄せて小さく出す
    constexpr float kEdgeRatio = 0.90f;
}

void HUDMinimap::Bake(ID3D11Device* device, const GridWorld& grid, const MapData::Map& map, int biome, uint32_t seed)
{
    m_Device = device;
    m_Image = MinimapBake::Build(grid, map, biome, seed);
    m_LastPos = { 1.0e9f, 0.0f, 0.0f };   // 次の Draw で窓を作り直す
    if (!m_Window && device)
    {
        m_Window = std::make_shared<Texture>();
        if (!m_Window->CreateDynamic(device, kN, kN)) m_Window.reset();
        m_Pixels.assign((size_t)kN * kN * 4, 0);
    }
}

void HUDMinimap::Clear()
{
    m_Image = MinimapBake::Image{};
}

// 双線形。texel の中心が (i + 0.5) * texel なので 0.5 ずらす。フィールドの外の角は透明として混ぜる
void HUDMinimap::Sample(float wx, float wz, float out[4]) const
{
    const float fx = (wx - m_Image.originX) / m_Image.texel - 0.5f;
    const float fz = (wz - m_Image.originZ) / m_Image.texel - 0.5f;
    const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
    const float tx = fx - ix, tz = fz - iz;
    const int W = m_Image.w, H = m_Image.h;
    const uint8_t* px = m_Image.rgba.data();

    float acc[4] = { 0, 0, 0, 0 };
    auto add = [&](int x, int z, float w)
        {
            if (w <= 0.0f || x < 0 || z < 0 || x >= W || z >= H) return;
            const uint8_t* p = px + ((size_t)z * W + x) * 4;
            acc[0] += p[0] * w; acc[1] += p[1] * w; acc[2] += p[2] * w; acc[3] += p[3] * w;
        };
    add(ix, iz, (1 - tx) * (1 - tz));
    add(ix + 1, iz, tx * (1 - tz));
    add(ix, iz + 1, (1 - tx) * tz);
    add(ix + 1, iz + 1, tx * tz);
    out[0] = acc[0]; out[1] = acc[1]; out[2] = acc[2]; out[3] = acc[3];
}

void HUDMinimap::UpdateWindow(ID3D11DeviceContext* context, const Vector3& playerPos, float range)
{
    if (!m_Window || !context || range <= 0.0f) return;
    // 動いていなければそのまま。窓の 1px = range / 96 ≈ 0.6m なので、0.25m 未満の移動では作り直さない
    // （走っている間も毎フレームではなく秒 20 回ほどの作り直しで済む。Debug の CPU 代を抑える）
    const Vector3 d = playerPos - m_LastPos;
    if (std::fabs(d.x) < 0.25f && std::fabs(d.z) < 0.25f && range == m_LastRange) return;
    m_LastPos = playerPos;
    m_LastRange = range;

    const float half = kN * 0.5f;
    const float mPerPx = range / half;
    uint8_t* dst = m_Pixels.data();
    for (int j = 0; j < kN; ++j)
    {
        const float sy = (j + 0.5f) - half;               // 画面の下向きが +
        const float wz = playerPos.z - sy * mPerPx;       // 北（+z）が上
        for (int i = 0; i < kN; ++i)
        {
            const float sx = (i + 0.5f) - half;
            const float wx = playerPos.x + sx * mPerPx;
            // 円のマスク（縁 1px を滑らかに）
            const float r = std::sqrt(sx * sx + sy * sy);
            const float mask = std::clamp(half - 1.0f - r + 0.5f, 0.0f, 1.0f);
            uint8_t* p = dst + ((size_t)j * kN + i) * 4;
            if (mask <= 0.0f) { p[0] = p[1] = p[2] = p[3] = 0; continue; }
            float c[4];
            Sample(wx, wz, c);
            p[0] = (uint8_t)std::clamp(c[0] + 0.5f, 0.0f, 255.0f);
            p[1] = (uint8_t)std::clamp(c[1] + 0.5f, 0.0f, 255.0f);
            p[2] = (uint8_t)std::clamp(c[2] + 0.5f, 0.0f, 255.0f);
            p[3] = (uint8_t)std::clamp(c[3] * mask + 0.5f, 0.0f, 255.0f);
        }
    }
    m_Window->Update(context, m_Pixels.data(), kN * 4);
}

void HUDMinimap::Draw(SpriteRenderer& sprite, TextRenderer& text, ID3D11DeviceContext* context,
    const HUDStyle& s, const HUDFrameInfo& info, const Vector2& center, const std::shared_ptr<Texture>& white)
{
    if (!s.showMinimap || !HasMap() || !info.hasPlayer || !white) return;
    const UIDeco::Textures& T = UIDeco::Tex();
    if (!T.disc || !T.ring) return;

    const float D = s.minimapSize;
    const float R = D * 0.5f;
    const Vector2 pos = { center.x - R, center.y - R };

    // ---- 地（暗い円）----
    sprite.Draw(T.disc, pos, { D, D }, s.minimapBgColor);

    // ---- 地形の窓 ----
    UpdateWindow(context, info.playerPos, s.minimapRange);
    if (m_Window)
        sprite.Draw(m_Window, pos, { D, D }, { 1.0f, 1.0f, 1.0f, s.minimapAlpha });

    // ---- 目印（箱・門・磁石・エリート・Boss。画面外の矢印と同じ色の菱形）----
    const float pxPerM = R / (std::max)(s.minimapRange, 0.01f);
    const float edge = R * kEdgeRatio;
    for (const HUDMarker& mk : info.markers)
    {
        Vector2 o = { (mk.position.x - info.playerPos.x) * pxPerM, -(mk.position.z - info.playerPos.z) * pxPerM };
        const float len = o.Length();
        float size = s.minimapMarkerSize;
        if (len > edge)
        {
            o *= edge / len;        // 窓の外の物は縁に寄せて小さく
            size *= 0.7f;
        }
        const Vector2 c = center + o;
        Vector4 col = mk.color;
        col.w = 0.95f;
        // 黒い縁取り（地形の上で見えるように）→ 本体
        const float os = size + 2.0f;
        sprite.Draw(white, { c.x - os * 0.5f, c.y - os * 0.5f }, { os, os }, { 0.0f, 0.0f, 0.0f, 0.75f }, kPi * 0.25f, c);
        sprite.Draw(white, { c.x - size * 0.5f, c.y - size * 0.5f }, { size, size }, col, kPi * 0.25f, c);
    }

    // ---- プレイヤー（中心の矢印。向き = TransformComponent::rotation.y、0 = +z = 上）----
    {
        const float yaw = DirectX::XMConvertToRadians(info.playerYawDeg);
        const float fx = std::sin(yaw), fz = std::cos(yaw);
        const float ang = std::atan2(-fz, fx);                 // 画面の角度（y 下向き）
        const float len = s.minimapPlayerSize;
        const float thick = (std::max)(2.0f, len * 0.26f);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        const Vector2 tip = center + dir * (len * 0.55f);
        // 「>」：先端から後ろへ 2 本（画面外の目印と同じ描き方）。黒い縁 → 本体
        for (int pass = 0; pass < 2; ++pass)
        {
            const float l = (pass == 0) ? len + 2.0f : len;
            const float t = (pass == 0) ? thick + 2.0f : thick;
            const Vector4 col = (pass == 0) ? Vector4(0.0f, 0.0f, 0.0f, 0.8f) : s.minimapPlayerColor;
            for (float spread : { 0.62f, -0.62f })
                sprite.Draw(white, { tip.x, tip.y - t * 0.5f }, { l, t }, col, ang + kPi + spread, tip);
        }
        // 足元の点
        const float dot = (std::max)(3.0f, len * 0.3f);
        sprite.Draw(T.disc, { center.x - dot * 0.5f, center.y - dot * 0.5f }, { dot, dot }, s.minimapPlayerColor);
    }

    // ---- 縁（古金の輪）と北の印 ----
    sprite.Draw(T.ring, pos, { D, D }, s.borderColor);
    {
        const float scale = s.minimapNorthScale;
        if (scale > 0.0f)
        {
            const Vector2 ts = text.Measure(L"N", scale);
            const Vector2 tp = { center.x - ts.x * 0.5f, center.y - R - ts.y * 0.5f };
            text.Draw(L"N", { tp.x + 1.0f, tp.y + 1.0f }, s.shadowColor, scale);
            text.Draw(L"N", tp, s.textColor, scale);
        }
    }
}
