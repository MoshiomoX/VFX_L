// ============================================================
// TitleLogo.cpp
// タイトルの名前の小片（UI/TitleLogo.h を参照）
// ============================================================
#include "UI/TitleLogo.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Material/Texture.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector4;

float TitleLogo::Rand01()
{
    m_Rng = m_Rng * 1664525u + 1013904223u;
    return (float)(m_Rng >> 8) / 16777216.0f;
}

// ============================================================
// 名前を絵に描いて読み戻し、小片に切る
// ============================================================
bool TitleLogo::Build(ID3D11Device* device, ID3D11DeviceContext* context, const Vector2& size, float margin, float tile,
    const DrawFn& draw)
{
    m_Texture.reset();
    m_Tiles.clear();
    if (!device || !context || size.x <= 1.0f || size.y <= 1.0f) return false;
    m_Size = size;
    m_Margin = std::ceil(margin);
    m_TileSize = (std::max)(2.0f, std::round(tile));
    const UINT w = (UINT)std::ceil(size.x + m_Margin * 2.0f);
    const UINT h = (UINT)std::ceil(size.y + m_Margin * 2.0f);

    // ---- 描く先（透明に消した RGBA8）と読み戻し用 ----
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> rt, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &rt))) return false;
    if (FAILED(device->CreateRenderTargetView(rt.Get(), nullptr, &rtv))) return false;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &staging))) return false;

    // ---- 今の描く先を預けて、名前を描いて戻す（SpriteBatch は今のビューポートに合わせて描く）----
    ComPtr<ID3D11RenderTargetView> oldRtv;
    ComPtr<ID3D11DepthStencilView> oldDsv;
    context->OMGetRenderTargets(1, oldRtv.GetAddressOf(), oldDsv.GetAddressOf());
    UINT vpCount = 1;
    D3D11_VIEWPORT oldVp = {};
    context->RSGetViewports(&vpCount, &oldVp);

    const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    context->ClearRenderTargetView(rtv.Get(), clear);
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    context->RSSetViewports(1, &vp);
    draw(Vector2(m_Margin, m_Margin));

    ID3D11RenderTargetView* restore = oldRtv.Get();
    context->OMSetRenderTargets(1, &restore, oldDsv.Get());
    if (vpCount > 0) context->RSSetViewports(1, &oldVp);

    // ---- 読み戻し：乗算済み → 素のアルファへ戻す（SpritePS がもう一度アルファを掛ける）----
    context->CopyResource(staging.Get(), rt.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    std::vector<uint8_t> pixels((size_t)w * h * 4);
    for (UINT y = 0; y < h; ++y)
    {
        const uint8_t* src = (const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch;
        uint8_t* dst = pixels.data() + (size_t)y * w * 4;
        for (UINT x = 0; x < w; ++x)
        {
            const uint8_t a = src[x * 4 + 3];
            for (int c = 0; c < 3; ++c)
                dst[x * 4 + c] = (a > 0) ? (uint8_t)(std::min)(255, src[x * 4 + c] * 255 / a) : 0;
            dst[x * 4 + 3] = a;
        }
    }
    context->Unmap(staging.Get(), 0);

    // ---- 何か描いてある所だけ小片にする ----
    const int T = (int)m_TileSize;
    for (int ty = 0; ty < (int)h; ty += T)
        for (int tx = 0; tx < (int)w; tx += T)
        {
            int maxA = 0;
            for (int y = ty; y < (std::min)(ty + T, (int)h); ++y)
                for (int x = tx; x < (std::min)(tx + T, (int)w); ++x)
                    maxA = (std::max)(maxA, (int)pixels[((size_t)y * w + x) * 4 + 3]);
            if (maxA < 3) continue;
            Tile t;
            t.home = Vector2((float)tx, (float)ty);
            t.seed = Rand01();
            m_Tiles.push_back(t);
        }

    auto tex = std::make_shared<Texture>();
    if (!tex->CreateFromMemory(device, pixels.data(), w, h)) { m_Tiles.clear(); return false; }
    m_Texture = tex;

    // 散った小片の光：縁へ柔らかく消える白い円（32px）
    if (!m_Glow)
    {
        constexpr int G = 32;
        std::vector<uint8_t> glow((size_t)G * G * 4);
        for (int y = 0; y < G; ++y)
            for (int x = 0; x < G; ++x)
            {
                const float dx = (x + 0.5f) / G * 2.0f - 1.0f, dy = (y + 0.5f) / G * 2.0f - 1.0f;
                const float r = (std::min)(1.0f, std::sqrt(dx * dx + dy * dy));
                const float a = (1.0f - r) * (1.0f - r);
                uint8_t* px = glow.data() + ((size_t)y * G + x) * 4;
                px[0] = px[1] = px[2] = 255;
                px[3] = (uint8_t)(a * 255.0f + 0.5f);
            }
        auto g = std::make_shared<Texture>();
        if (g->CreateFromMemory(device, glow.data(), G, G)) m_Glow = g;
    }
    m_TexSize = Vector2((float)w, (float)h);
    m_BurstAge = -1.0f;
    return true;
}

// ============================================================
// 毎フレーム：そよ風・マウスで散らし、漂わせてからばねで戻す
// ============================================================
void TitleLogo::Update(float dt, const Vector2& origin, const Vector2& mouse, const Vector2& mouseVel, float k)
{
    if (!Ready()) return;
    dt = (std::min)(dt, 0.05f);
    m_Time += dt;
    m_K = k;
    const Settings& s = settings;
    const float T = m_TileSize;
    const Vector2 base = origin - Vector2(m_Margin, m_Margin);
    const Vector2 wind(1.0f, -0.25f);   // 右へ、少し上へ（画面の y は下向き）

    if (m_BurstAge >= 0.0f)
    {
        m_BurstAge += dt;
        for (Tile& t : m_Tiles)
        {
            t.vel += wind * (900.0f * k * dt);
            t.vel *= std::exp(-0.9f * dt);
            t.off += t.vel * dt;
        }
        return;
    }

    // ---- そよ風：時々、名前を左から右へ横切って一部の小片をさらう ----
    m_BreezeTimer -= dt;
    if (m_BreezeTimer <= 0.0f && m_SweepX < 0.0f)
    {
        m_SweepX = 0.0f;
        ++m_SweepId;
        m_BreezeTimer = s.breezeMin + (s.breezeMax - s.breezeMin) * Rand01();
    }
    const float texW = (std::max)(m_TexSize.x, 1.0f);
    if (m_SweepX >= 0.0f)
    {
        m_SweepX += dt / (std::max)(s.breezeSweep, 0.05f);
        if (m_SweepX > 1.1f) m_SweepX = -1.0f;
    }

    const float mouseSpeed = mouseVel.Length();
    const bool mouseOn = mouseSpeed > s.mouseMinSpeed * k;
    const float radius = s.mouseRadius * k;
    for (Tile& t : m_Tiles)
    {
        // そよ風
        if (m_SweepX >= 0.0f && t.sweep != m_SweepId && std::fabs(t.home.x / texW - m_SweepX) < 0.035f)
        {
            t.sweep = m_SweepId;
            if (Rand01() < s.breezeChance)
            {
                const float f = 0.6f + 0.8f * Rand01();
                t.vel += Vector2(wind.x * f, wind.y * f - 0.3f * Rand01()) * (s.breezeKick * k);
                t.hold = (std::max)(t.hold, s.holdMin + (s.holdMax - s.holdMin) * Rand01());
            }
        }
        // マウス
        if (mouseOn)
        {
            const Vector2 c = base + t.home + Vector2(T * 0.5f, T * 0.5f) + t.off;
            const Vector2 d = c - mouse;
            const float dist = d.Length();
            if (dist < radius)
            {
                const float f = 1.0f - dist / radius;
                const Vector2 side(-mouseVel.y, mouseVel.x);
                t.vel += mouseVel * (s.mouseKick * f) + side * ((Rand01() - 0.5f) * 0.4f * f);
                t.hold = (std::max)(t.hold, s.holdMin + (s.holdMax - s.holdMin) * f);
            }
        }

        // 動かす：散っている間は漂い、終わったらばねで元の所へ
        if (t.hold > 0.0f)
        {
            t.hold -= dt;
            t.vel *= std::exp(-s.drag * dt);
            const float ph = m_Time * 3.0f + t.seed * 6.2831853f;   // 少し揺らぐ（ばらばらに漂う）
            t.vel += Vector2(std::sin(ph), std::cos(ph * 1.3f)) * (40.0f * k * dt);
            t.off += t.vel * dt;
        }
        else if (t.off.LengthSquared() > 0.0f || t.vel.LengthSquared() > 0.0f)
        {
            t.vel += (-t.off * s.spring - t.vel * s.springDamp) * dt;
            t.off += t.vel * dt;
            if (t.off.LengthSquared() < 0.09f && t.vel.LengthSquared() < 25.0f)
            {
                t.off = Vector2::Zero;
                t.vel = Vector2::Zero;
            }
        }
    }
}

void TitleLogo::Burst()
{
    if (!Ready() || m_BurstAge >= 0.0f) return;
    m_BurstAge = 0.0f;
    const Vector2 center = m_TexSize * 0.5f;
    for (Tile& t : m_Tiles)
    {
        Vector2 d = t.home - center;
        if (d.LengthSquared() < 1.0f) d = Vector2(1.0f, 0.0f);
        d.Normalize();
        const float speed = (250.0f + 650.0f * Rand01()) * m_K;
        t.vel = d * speed + Vector2(200.0f, -120.0f - 200.0f * Rand01()) * m_K;
        t.hold = 1.0e9f;
    }
}

// ============================================================
// 描く：元の所の小片はそのまま、散った小片は小さく明るく、後ろに光の粒
// ============================================================
void TitleLogo::Draw(SpriteRenderer& sprite, const Vector2& origin, const Vector4& tint, const std::shared_ptr<Texture>& glow) const
{
    if (!Ready()) return;
    const float T = m_TileSize;
    const Vector2 base = Vector2(std::round(origin.x), std::round(origin.y)) - Vector2(m_Margin, m_Margin);
    float fade = tint.w;
    if (m_BurstAge >= 0.0f) fade *= (std::max)(0.0f, 1.0f - m_BurstAge / (std::max)(settings.burstTime, 0.05f));
    if (fade <= 0.0f) return;
    const float spread = 18.0f * m_K;

    // 光の粒（小片の下）。柔らかい円を HDR で（bloom でにじむ）
    const std::shared_ptr<Texture>& spark = m_Glow ? m_Glow : glow;
    if (spark)
        for (const Tile& t : m_Tiles)
        {
            const float disp = (std::min)(t.off.Length() / spread, 1.0f);
            if (disp < 0.05f) continue;
            const float g = T * (0.9f + 1.3f * disp);
            const Vector2 c = base + t.home + Vector2(T * 0.5f, T * 0.5f) + t.off;
            sprite.Draw(spark, c - Vector2(g * 0.5f, g * 0.5f), Vector2(g, g),
                Vector4(2.4f, 1.6f, 0.6f, 0.55f * disp * settings.sparkGain * fade));
        }

    for (const Tile& t : m_Tiles)
    {
        const float disp = (std::min)(t.off.Length() / spread, 1.0f);
        const float sz = T * (1.0f - 0.45f * disp);
        Vector2 pos = base + t.home + Vector2((T - sz) * 0.5f, (T - sz) * 0.5f) + t.off;
        if (disp <= 0.0f) pos = Vector2(std::round(pos.x), std::round(pos.y));
        const Vector4 uv(t.home.x / m_TexSize.x, t.home.y / m_TexSize.y, T / m_TexSize.x, T / m_TexSize.y);
        const float bright = 1.0f + 0.7f * disp;
        sprite.Draw(m_Texture, pos, Vector2(sz, sz),
            Vector4(tint.x * bright, tint.y * bright, tint.z * bright, fade * (1.0f - 0.25f * disp)), uv);
    }
}
