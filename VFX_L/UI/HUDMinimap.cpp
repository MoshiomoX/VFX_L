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
    m_HasWindow = false;   // 次の Draw で窓を全部作り直す
    if (!m_Window && device)
    {
        m_Window = std::make_shared<Texture>();
        if (!m_Window->CreateDynamic(device, kN, kN)) m_Window.reset();
        m_Pixels.assign((size_t)kN * kN * 4, 0);   // 円の外は 0 のまま一度も書かない
        m_Color.assign((size_t)kN * kN * 4, 0);
        m_Scratch.assign((size_t)kN * kN * 4, 0);
        BuildMask();
    }
}

// 円のマスク（縁 1px を滑らかに）。窓の位置だけで決まるので 1 回だけ
void HUDMinimap::BuildMask()
{
    const float half = kN * 0.5f;
    m_Mask.assign((size_t)kN * kN, 0);
    m_RowSpan.assign((size_t)kN * 2, 0);
    for (int j = 0; j < kN; ++j)
    {
        int i0 = kN, i1 = 0;
        const float sy = (j + 0.5f) - half;
        for (int i = 0; i < kN; ++i)
        {
            const float sx = (i + 0.5f) - half;
            const float r = std::sqrt(sx * sx + sy * sy);
            const float mask = std::clamp(half - 1.0f - r + 0.5f, 0.0f, 1.0f);
            m_Mask[(size_t)j * kN + i] = (uint8_t)(mask * 255.0f + 0.5f);
            if (mask > 0.0f) { i0 = (std::min)(i0, i); i1 = (std::max)(i1, i + 1); }
        }
        m_RowSpan[(size_t)j * 2] = i0;
        m_RowSpan[(size_t)j * 2 + 1] = (std::max)(i0, i1);
    }
}

void HUDMinimap::Clear()
{
    m_Image = MinimapBake::Image{};
    m_HasWindow = false;
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

// 窓の px (i, j) の中心の世界座標 = ((cellX + kN/2 - 0.5 - i) * m, (cellZ + kN/2 - 0.5 - j) * m)。
// 右へ行くほど -x、下へ行くほど -z（右手系を真上から +z を上にして見た向き）
void HUDMinimap::SampleSpan(int j, int i0, int i1)
{
    const float m = m_CellM;
    const float baseX = (m_CellX + kN * 0.5f - 0.5f) * m;
    const float wz = (m_CellZ + kN * 0.5f - 0.5f - j) * m;
    uint8_t* row = m_Color.data() + (size_t)j * kN * 4;
    for (int i = i0; i < i1; ++i)
    {
        float c[4];
        Sample(baseX - i * m, wz, c);
        uint8_t* p = row + (size_t)i * 4;
        for (int k = 0; k < 4; ++k)
        {
            const float v = c[k] + 0.5f;
            p[k] = (uint8_t)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
        }
    }
}

void HUDMinimap::UpdateWindow(ID3D11DeviceContext* context, const Vector3& playerPos, float range)
{
    if (!m_Window || !context || range <= 0.0f || m_Mask.empty()) return;
    // 中心を窓の 1px（range / 96 ≈ 0.6m）の格子に揃える。格子が変わらない間は作り直さない
    const float m = range / (kN * 0.5f);
    const int cellX = (int)std::floor(playerPos.x / m + 0.5f);
    const int cellZ = (int)std::floor(playerPos.z / m + 0.5f);
    // 端数（窓の px）。窓はこれだけずらして描く。プレイヤーが格子の中心より +x（画面の左）にいる
    // → 格子の中心はプレイヤーから見て右なので窓は右へ。+z（上）にいる → 窓は下へ
    m_SubPx = { (playerPos.x - cellX * m) / m, (playerPos.z - cellZ * m) / m };
    if (m_HasWindow && m == m_CellM && cellX == m_CellX && cellZ == m_CellZ) return;

    // 窓の px で何マスずれたか。新しい px (i, j) = 前の px (i - dx, j - dz)
    const int dx = cellX - m_CellX, dz = cellZ - m_CellZ;
    const bool scroll = m_HasWindow && m == m_CellM && std::abs(dx) < kN && std::abs(dz) < kN;
    m_CellX = cellX;
    m_CellZ = cellZ;
    m_CellM = m;
    m_HasWindow = true;

    if (!scroll)
    {
        for (int j = 0; j < kN; ++j)
            SampleSpan(j, m_RowSpan[(size_t)j * 2], m_RowSpan[(size_t)j * 2 + 1]);
    }
    else
    {
        // 前の絵をずらして写し、はみ出た所だけ切り出す（円の外は使わないので行ごとの範囲だけ）
        m_Color.swap(m_Scratch);   // m_Scratch = 前の絵
        for (int j = 0; j < kN; ++j)
        {
            const int i0 = m_RowSpan[(size_t)j * 2], i1 = m_RowSpan[(size_t)j * 2 + 1];
            if (i0 >= i1) continue;
            const int jo = j - dz;
            if (jo < 0 || jo >= kN) { SampleSpan(j, i0, i1); continue; }
            // 写せるのは前の絵の行 jo の中で中身がある所（円の内側 [si0, si1)）だけ。円の外は一度も切り出していない（透明）ので、
            // 正方形の範囲で写すと透明が新しい円の中へ入って暗い三日月になり、そのまま残っていた（2026-10-24 ユーザー報告）
            const int si0 = m_RowSpan[(size_t)jo * 2], si1 = m_RowSpan[(size_t)jo * 2 + 1];
            const int a = (std::max)(i0, si0 + dx), b = (std::min)(i1, si1 + dx);
            if (a >= b) { SampleSpan(j, i0, i1); continue; }
            std::memcpy(m_Color.data() + ((size_t)j * kN + a) * 4,
                m_Scratch.data() + ((size_t)jo * kN + (a - dx)) * 4, (size_t)(b - a) * 4);
            if (i0 < a) SampleSpan(j, i0, a);
            if (b < i1) SampleSpan(j, b, i1);
        }
    }

    // 円のマスクを掛けて GPU へ
    for (int j = 0; j < kN; ++j)
    {
        const int i0 = m_RowSpan[(size_t)j * 2], i1 = m_RowSpan[(size_t)j * 2 + 1];
        const uint8_t* src = m_Color.data() + ((size_t)j * kN + i0) * 4;
        const uint8_t* mask = m_Mask.data() + (size_t)j * kN + i0;
        uint8_t* dst = m_Pixels.data() + ((size_t)j * kN + i0) * 4;
        for (int i = i0; i < i1; ++i, src += 4, dst += 4, ++mask)
        {
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
            dst[3] = (uint8_t)((src[3] * (unsigned)*mask + 127u) / 255u);
        }
    }
    m_Window->Update(context, m_Pixels.data(), kN * 4);
}

void HUDMinimap::DrawMarkerBadge(SpriteRenderer& sprite, const std::shared_ptr<Texture>& icon,
    const Vector2& c, float size, const Vector4& color)
{
    const UIDeco::Textures& T = UIDeco::Tex();
    if (!T.disc) return;
    const float b = size * 1.5f;                              // 丸の直径
    const float edge = (std::max)(1.5f, size * 0.11f);        // 色の縁の太さ
    const float inner = b - edge * 2.0f;
    sprite.Draw(T.disc, { c.x - b * 0.5f, c.y - b * 0.5f }, { b, b }, { color.x, color.y, color.z, 0.95f });
    sprite.Draw(T.disc, { c.x - inner * 0.5f, c.y - inner * 0.5f }, { inner, inner }, { 0.03f, 0.03f, 0.045f, 0.88f });
    if (!icon) return;
    // 暗い丸の上で読めるよう、目印の色を白へ 35% 寄せる
    const Vector4 ic = { color.x + (1.0f - color.x) * 0.35f, color.y + (1.0f - color.y) * 0.35f, color.z + (1.0f - color.z) * 0.35f, 1.0f };
    sprite.Draw(icon, { c.x - size * 0.5f, c.y - size * 0.5f }, { size, size }, ic);
}

void HUDMinimap::Draw(SpriteRenderer& sprite, TextRenderer& text, ID3D11DeviceContext* context,
    const HUDStyle& s, const HUDFrameInfo& info, const Vector2& center, const std::shared_ptr<Texture>& white,
    const std::shared_ptr<Texture>* icons)
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
    if (m_Window && m_HasWindow)
        sprite.Draw(m_Window, pos + m_SubPx * (D / kN), { D, D }, { 1.0f, 1.0f, 1.0f, s.minimapAlpha });

    // ---- 目印（箱・門・出口・磁石・Boss・エリート・押し寄せ。種類ごとの絵 + 画面外の矢印と同じ色の縁）----
    const float pxPerM = R / (std::max)(s.minimapRange, 0.01f);
    const float edge = R * kEdgeRatio;
    for (const HUDMarker& mk : info.markers)
    {
        // 右手系を真上から見るので +x は左、+z は上
        Vector2 o = { -(mk.position.x - info.playerPos.x) * pxPerM, -(mk.position.z - info.playerPos.z) * pxPerM };
        const float len = o.Length();
        float size = s.minimapMarkerSize;
        if (len > edge)
        {
            o *= edge / len;        // 窓の外の物は縁に寄せて小さく
            size *= 0.75f;
        }
        const int k = (int)mk.kind;
        const std::shared_ptr<Texture> none;
        DrawMarkerBadge(sprite, (icons && k >= 0 && k < (int)HUDMarkerKind::Count) ? icons[k] : none, center + o, size, mk.color);
    }

    // ---- プレイヤー（中心の矢印。向き = TransformComponent::rotation.y、0 = +z = 上）----
    {
        const float yaw = DirectX::XMConvertToRadians(info.playerYawDeg);
        const float fx = std::sin(yaw), fz = std::cos(yaw);
        const float ang = std::atan2(-fz, -fx);                // 画面の角度（y 下向き。+x は画面の左）
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
