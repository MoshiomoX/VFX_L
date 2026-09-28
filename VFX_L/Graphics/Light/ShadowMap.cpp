// ============================================================
// ShadowMap.cpp
// ============================================================
#include "Graphics/Light/ShadowMap.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Camera/CameraBase.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <iostream>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

namespace
{
    constexpr UINT kShadowSRVSlot = 9;       // Lighting.hlsli の g_ShadowMap
    constexpr UINT kShadowSamplerSlot = 2;   // Lighting.hlsli の g_ShadowSampler
}

bool ShadowMap::Initialize(ID3D11Device* device)
{
    m_Device = device;
    if (!device) return false;

    // TEMP-TEST: 自己テストで段の色分けを最初から出す
    char env[8] = {};
    if (GetEnvironmentVariableA("VFXL_SHADOW_CASCADES", env, sizeof(env)) > 0)
        m_Settings.showCascades = true;
    if (GetEnvironmentVariableA("VFXL_NO_SHADOW", env, sizeof(env)) > 0)
        m_Settings.enabled = false;   // 影の有無で負荷を比べる

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;   // 比較の結果を 2x2 で補間
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0f;   // 図の外 = 日向
    sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sd.MaxLOD = FLT_MAX;
    if (FAILED(device->CreateSamplerState(&sd, &m_CompareSampler)))
    {
        std::cout << "[Error] ShadowMap: sampler create failed" << std::endl;
        return false;
    }
    return CreateMaps(m_Settings.size) && CreateRasterState();
}

void ShadowMap::Shutdown()
{
    for (auto& d : m_DSV) d.Reset();
    m_SRV.Reset();
    m_Texture.Reset();
    m_CompareSampler.Reset();
    m_Raster.Reset();
    m_MapSize = 0;
    m_Device = nullptr;
}

// 1 段 size x size の D32F を 3 層。描く時は層ごとの DSV、読む時は配列の SRV
bool ShadowMap::CreateMaps(int size)
{
    for (auto& d : m_DSV) d.Reset();
    m_SRV.Reset();
    m_Texture.Reset();
    m_MapSize = 0;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = (UINT)size;
    td.MipLevels = 1;
    td.ArraySize = kCascades;
    td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_Texture)))
    {
        std::cout << "[Error] ShadowMap: texture create failed (" << size << ")" << std::endl;
        return false;
    }

    for (int c = 0; c < kCascades; ++c)
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dd.Texture2DArray.FirstArraySlice = (UINT)c;
        dd.Texture2DArray.ArraySize = 1;
        if (FAILED(m_Device->CreateDepthStencilView(m_Texture.Get(), &dd, &m_DSV[c])))
        {
            std::cout << "[Error] ShadowMap: DSV create failed" << std::endl;
            return false;
        }
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srd = {};
    srd.Format = DXGI_FORMAT_R32_FLOAT;
    srd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srd.Texture2DArray.MipLevels = 1;
    srd.Texture2DArray.ArraySize = kCascades;
    if (FAILED(m_Device->CreateShaderResourceView(m_Texture.Get(), &srd, &m_SRV)))
    {
        std::cout << "[Error] ShadowMap: SRV create failed" << std::endl;
        return false;
    }
    m_MapSize = size;
    return true;
}

// 描く側の状態：両面（薄い物・向きの揃っていないモデルも影を落とす）+ 深度バイアス。
// 深度クリップ無し = 光源の近い面より手前の物も 0 に潰して描く（影が欠けない）
bool ShadowMap::CreateRasterState()
{
    m_Raster.Reset();
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthBias = m_Settings.rasterBias;
    rd.SlopeScaledDepthBias = m_Settings.slopeBias;
    rd.DepthClipEnable = FALSE;
    if (FAILED(m_Device->CreateRasterizerState(&rd, &m_Raster)))
    {
        std::cout << "[Error] ShadowMap: rasterizer create failed" << std::endl;
        return false;
    }
    m_RasterBias = m_Settings.rasterBias;
    m_RasterSlope = m_Settings.slopeBias;
    return true;
}

void ShadowMap::Render(ID3D11DeviceContext* ctx, Renderer& renderer, CameraBase& camera,
    const Vector3& sunDir, const DrawCasters& drawCasters)
{
    if (!ctx || !m_Device || !m_Settings.enabled)
    {
        Disable(renderer);
        return;
    }
    if (m_Settings.size != m_MapSize && !CreateMaps(m_Settings.size))
    {
        m_Settings.size = 2048;
        if (!CreateMaps(m_Settings.size)) { Disable(renderer); return; }
    }
    if ((m_Settings.rasterBias != m_RasterBias || m_Settings.slopeBias != m_RasterSlope) && !CreateRasterState())
    {
        Disable(renderer);
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();

    // ---- 段の球（カメラ空間で出して世界へ）----
    // 視錐台を距離 dn..df で切った塊の 8 隅の重心と、そこから一番遠い隅。
    // 受け持ちは「カメラからの距離」なので、手前側は画面の隅の方向でも入るように cos 分だけ近くから取る
    const Matrix view = camera.GetViewMatrix();
    const Matrix proj = camera.GetProjectionMatrix();
    const Matrix invView = view.Invert();
    const float tanX = 1.0f / proj._11;
    const float tanY = 1.0f / proj._22;
    const float cosCorner = 1.0f / std::sqrt(1.0f + tanX * tanX + tanY * tanY);

    Vector3 dir = sunDir;
    dir.Normalize();
    const Vector3 up = (std::fabs(dir.y) > 0.99f) ? Vector3(0.0f, 0.0f, 1.0f) : Vector3(0.0f, 1.0f, 0.0f);
    const Matrix lightRot = Matrix::CreateLookAt(Vector3::Zero, dir, up);
    const Matrix lightRotInv = lightRot.Invert();
    const float size = (float)m_MapSize;
    const float back = (std::max)(m_Settings.backDistance, 0.0f);

    Matrix viewProj[kCascades];
    float texelWorld[kCascades] = {};

    // 段の境目は単調に増やす（面板で逆にしても壊れない）
    float splits[kCascades];
    for (int c = 0; c < kCascades; ++c)
        splits[c] = (std::max)(m_Settings.splits[c], (c > 0 ? splits[c - 1] : 0.0f) + 1.0f);

    // ---- 描く ----
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(kShadowSRVSlot, 1, &nullSRV);   // 前フレームの読み取りを外してから DSV に
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, size, size, 0.0f, 1.0f };
    ctx->RSSetViewports(1, &vp);
    const float blendFactor[4] = { 0, 0, 0, 0 };
    auto& rs = RenderStates::Get();

    for (int c = 0; c < kCascades; ++c)
    {
        const float dn = (c == 0) ? 0.0f : splits[c - 1] * cosCorner;
        const float df = splits[c];
        Vector3 corners[8];
        int k = 0;
        for (float d : { dn, df })
            for (float sy : { -1.0f, 1.0f })
                for (float sx : { -1.0f, 1.0f })
                    corners[k++] = Vector3(sx * d * tanX, sy * d * tanY, -d);   // 右手系: 前 = -Z
        Vector3 centerV = Vector3::Zero;
        for (const Vector3& p : corners) centerV += p;
        centerV /= 8.0f;
        float r = 0.0f;
        for (const Vector3& p : corners) r = (std::max)(r, (p - centerV).Length());
        r = std::ceil(r * 2.0f) * 0.5f;   // 0.5m 単位に丸める（浮動小数の揺れで大きさが変わらない）

        // 中心を光源から見て 1 texel 単位に揃える
        const float texel = 2.0f * r / size;
        Vector3 cl = Vector3::Transform(Vector3::Transform(centerV, invView), lightRot);
        cl.x = std::floor(cl.x / texel) * texel;
        cl.y = std::floor(cl.y / texel) * texel;
        const Vector3 center = Vector3::Transform(cl, lightRotInv);

        const Matrix lview = Matrix::CreateLookAt(center - dir * (r + back), center, up);
        const Matrix lproj = Matrix::CreateOrthographic(2.0f * r, 2.0f * r, 0.0f, 2.0f * r + back);
        viewProj[c] = lview * lproj;
        texelWorld[c] = texel;

        ctx->ClearDepthStencilView(m_DSV[c].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        ctx->OMSetRenderTargets(0, nullptr, m_DSV[c].Get());
        ctx->RSSetState(m_Raster.Get());
        ctx->OMSetDepthStencilState(rs.DepthDefault(), 0);
        ctx->OMSetBlendState(rs.Opaque(), blendFactor, 0xFFFFFFFF);

        renderer.BeginDepthPass(lview, lproj);
        if (drawCasters) drawCasters(lview, lproj, c);
        renderer.EndDepthPass();
    }
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    rs.Restore(ctx);

    // ---- 受け取り側へ ----
    renderer.SetShadow(viewProj,
        Vector4(splits[0], splits[1], splits[2], 1.0f),
        Vector4(texelWorld[0], texelWorld[1], texelWorld[2], 0.0f),
        Vector4(1.0f / size, m_Settings.normalOffset, std::clamp(m_Settings.strength, 0.0f, 1.0f), (float)(std::max)(m_Settings.pcfRadius, 0)),
        Vector4(std::clamp(m_Settings.fadeStart, 0.0f, 1.0f), m_Settings.compareBias, m_Settings.showCascades ? 1.0f : 0.0f, 0.0f));
    ID3D11ShaderResourceView* srv = m_SRV.Get();
    ctx->PSSetShaderResources(kShadowSRVSlot, 1, &srv);
    ID3D11SamplerState* samp = m_CompareSampler.Get();
    ctx->PSSetSamplers(kShadowSamplerSlot, 1, &samp);

    m_LastPassMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void ShadowMap::Unbind(ID3D11DeviceContext* ctx) const
{
    if (!ctx) return;
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(kShadowSRVSlot, 1, &nullSRV);
}

void ShadowMap::Disable(Renderer& renderer) const
{
    renderer.ClearShadow();
}

void ShadowMap::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Shadows")) return;

    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##shadow", &s.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Show Cascades", &s.showCascades);

    static const int kSizes[] = { 1024, 2048, 4096 };
    int sizeIdx = (s.size <= 1024) ? 0 : (s.size <= 2048) ? 1 : 2;
    if (ImGui::Combo("Map Size", &sizeIdx, "1024\0" "2048\0" "4096 (192 MB)\0"))
        s.size = kSizes[sizeIdx];

    ImGui::DragFloat3("Splits (m)", s.splits, 0.5f, 1.0f, 400.0f, "%.0f");
    ImGui::DragFloat("Back Distance", &s.backDistance, 1.0f, 0.0f, 300.0f, "%.0f m");
    ImGui::SliderFloat("Strength", &s.strength, 0.0f, 1.0f);
    ImGui::SliderInt("PCF Radius", &s.pcfRadius, 0, 3);
    ImGui::SliderFloat("Fade Start", &s.fadeStart, 0.0f, 1.0f);
    ImGui::DragFloat("Normal Offset", &s.normalOffset, 0.05f, 0.0f, 10.0f, "%.2f texel");
    ImGui::DragFloat("Compare Bias", &s.compareBias, 0.00001f, 0.0f, 0.01f, "%.5f");
    ImGui::DragInt("Raster Bias", &s.rasterBias, 10.0f, 0, 100000);
    ImGui::DragFloat("Slope Bias", &s.slopeBias, 0.05f, 0.0f, 10.0f);
    ImGui::Text("shadow pass (CPU): %.2f ms", m_LastPassMs);
}
