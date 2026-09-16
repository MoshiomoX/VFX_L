// ============================================================
// Graphics.cpp
// ============================================================
#include "Graphics/Graphics.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Graphics/Renderer/RenderStates.h"
#include <iostream>
#include <dxgi.h>

#define DX_CHECK(hr, msg) \
    if (FAILED(hr)) { \
        std::cout << "[DX ERROR] " << msg << " (HRESULT: 0x" << std::hex << hr << std::dec << ")" << std::endl; \
        return false; \
    }

namespace
{
    // 場面 RT の形式。粒子の加算が 1.0 を超えて残るようにする（bloom の入力）
    constexpr DXGI_FORMAT kSceneFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    constexpr DXGI_FORMAT kBackbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
}

// ============================================================
// 初期化
// ============================================================
bool Graphics::Initialize(HWND hWnd, int width, int height)
{
    if (!IsWindow(hWnd))
    {
        std::cout << "[ERROR] Invalid HWND" << std::endl;
        return false;
    }

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION,
        &m_Device, nullptr, &m_Context);
    DX_CHECK(hr, "D3D11CreateDevice failed");

    // ---- MSAA: backbuffer と HDR RT の両方が対応する品質を取る ----
    UINT qBack = 0, qScene = 0;
    m_Device->CheckMultisampleQualityLevels(kBackbufferFormat, 4, &qBack);
    m_Device->CheckMultisampleQualityLevels(kSceneFormat, 4, &qScene);
    const UINT q = (qBack < qScene) ? qBack : qScene;
    m_SampleCount = (q > 0) ? 4 : 1;
    m_SampleQuality = (q > 0) ? q - 1 : 0;
    std::cout << "[Info] MSAA: " << m_SampleCount << "x (Quality: " << m_SampleQuality << ")" << std::endl;

    // ---- swap chain ----
    ComPtr<IDXGIDevice> dxgiDevice;
    m_Device.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIFactory> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = kBackbufferFormat;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWnd;
    scd.SampleDesc.Count = m_SampleCount;
    scd.SampleDesc.Quality = m_SampleQuality;
    scd.Windowed = TRUE;

    hr = factory->CreateSwapChain(m_Device.Get(), &scd, &m_SwapChain);
    DX_CHECK(hr, "CreateSwapChain failed");

    if (!CreateSceneTargets(width, height)) return false;
    if (!LoadPostShaders()) return false;
    if (!m_Bloom.Initialize(m_Device.Get(), width, height)) return false;
    m_Context->OMSetRenderTargets(1, m_SceneRTV.GetAddressOf(), m_DepthStencilView.Get());
    m_Context->RSSetViewports(1, &m_Viewport);

  
    
    std::cout << "[OK] Graphics initialized" << std::endl;
    return true;
}

// ============================================================
// backbuffer RTV / HDR RT / resolve 先 / 深度 をまとめて作る
// 起動時と Resize の両方から呼ぶ
// ============================================================
bool Graphics::CreateSceneTargets(int width, int height)
{
    // ---- backbuffer ----
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    DX_CHECK(hr, "GetBuffer failed");
    hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_BackbufferRTV);
    DX_CHECK(hr, "CreateRenderTargetView(backbuffer) failed");

    // ---- 場面用 HDR RT（MSAA）----
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = kSceneFormat;
    td.SampleDesc.Count = m_SampleCount;
    td.SampleDesc.Quality = m_SampleQuality;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = m_Device->CreateTexture2D(&td, nullptr, &m_SceneTex);
    DX_CHECK(hr, "CreateTexture2D(scene) failed");
    hr = m_Device->CreateRenderTargetView(m_SceneTex.Get(), nullptr, &m_SceneRTV);
    DX_CHECK(hr, "CreateRenderTargetView(scene) failed");

    // ---- resolve 先（非 MSAA, 後処理が読む）----
    td.SampleDesc.Count = 1;
    td.SampleDesc.Quality = 0;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = m_Device->CreateTexture2D(&td, nullptr, &m_SceneResolved);
    DX_CHECK(hr, "CreateTexture2D(sceneResolved) failed");
    hr = m_Device->CreateShaderResourceView(m_SceneResolved.Get(), nullptr, &m_SceneSRV);
    DX_CHECK(hr, "CreateShaderResourceView(sceneResolved) failed");

    // ---- 深度（HDR RT と同じ MSAA）----
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = width;
    dd.Height = height;
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = kDepthFormat;
    dd.SampleDesc.Count = m_SampleCount;
    dd.SampleDesc.Quality = m_SampleQuality;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depthTex;
    hr = m_Device->CreateTexture2D(&dd, nullptr, &depthTex);
    DX_CHECK(hr, "CreateTexture2D(depth) failed");
    hr = m_Device->CreateDepthStencilView(depthTex.Get(), nullptr, &m_DepthStencilView);
    DX_CHECK(hr, "CreateDepthStencilView failed");

    m_Viewport.Width = (float)width;
    m_Viewport.Height = (float)height;
    m_Viewport.TopLeftX = 0.0f;
    m_Viewport.TopLeftY = 0.0f;
    m_Viewport.MinDepth = 0.0f;
    m_Viewport.MaxDepth = 1.0f;
    return true;
}

// ============================================================
// 合成用シェーダー
// ============================================================
bool Graphics::LoadPostShaders()
{
    m_CompositeVS = std::make_shared<VertexShader>();
    HRESULT hr = ShaderPath::Load(m_CompositeVS.get(), m_Device.Get(),
        L"Shader/PostProcess/CompositeVS.hlsl");
    std::cout << "[Graphics] CompositeVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_CompositePS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_CompositePS.get(), m_Device.Get(),
        L"Shader/PostProcess/CompositePS.hlsl");
    std::cout << "[Graphics] CompositePS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    return true;
}

// ============================================================
// フレーム開始: HDR RT を clear して bind
// ============================================================
void Graphics::BeginFrame()
{
    m_InUIPhase = false;

    const float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
    m_Context->ClearRenderTargetView(m_SceneRTV.Get(), clearColor);
    m_Context->ClearDepthStencilView(m_DepthStencilView.Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    m_Context->OMSetRenderTargets(1, m_SceneRTV.GetAddressOf(), m_DepthStencilView.Get());
    m_Context->RSSetViewports(1, &m_Viewport);
}

// ============================================================
// 場面描画の終わり: resolve → 合成 → backbuffer を bind
// これ以降の描画（UI / ImGui）は backbuffer に乗る
// ============================================================
void Graphics::BeginUI()
{
    m_InUIPhase = true;

    // 1) MSAA を解く
    m_Context->ResolveSubresource(m_SceneResolved.Get(), 0, m_SceneTex.Get(), 0, kSceneFormat);

    // 2) bloom（CS）。無効なら走らせず、合成側で倍率 0 にする
    const BloomParams& bp = m_Bloom.Params();
    if (bp.enabled)
        m_Bloom.Execute(m_Context.Get(), m_SceneSRV.Get());

    // 3) backbuffer へ全画面合成（深度は使わない）
    m_Context->OMSetRenderTargets(1, m_BackbufferRTV.GetAddressOf(), nullptr);
    m_Context->RSSetViewports(1, &m_Viewport);

    struct CompositeCB { float intensity; float exposure; uint32_t tonemap; uint32_t gamma; } ccb = {};
    ccb.intensity = bp.enabled ? bp.intensity : 0.0f;
    ccb.exposure = bp.exposure;
    ccb.tonemap = bp.tonemap ? 1u : 0u;
    ccb.gamma = bp.gamma ? 1u : 0u;

    ID3D11SamplerState* samp = RenderStates::Get().LinearClamp();
    m_Context->PSSetSamplers(0, 1, &samp);
    ID3D11ShaderResourceView* srvs[2] = { m_SceneSRV.Get(), m_Bloom.GetResultSRV() };
    m_Context->PSSetShaderResources(0, 2, srvs);

    RenderStates::Get().ApplyOpaque(m_Context.Get());
    m_CompositeVS->Bind(m_Context.Get());
    m_CompositePS->Bind(m_Context.Get());

    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    m_Context->Draw(3, 0);

    // 次のフレームで RT / UAV になるので SRV から外す
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    m_Context->PSSetShaderResources(0, 2, nullSRVs);
    RenderStates::Get().Restore(m_Context.Get());
}

void Graphics::EndFrame()
{
    m_SwapChain->Present(1, 0);
}

void Graphics::Shutdown()
{
    m_Bloom.Shutdown();
    m_CompositeVS.reset();
    m_CompositePS.reset();
}

// ============================================================
// 今の段階に合った RT に戻す
// 場面中に別の RT へ描いた後（影など）はここで HDR RT に戻る。
// UI 中に呼ばれたら backbuffer に戻る
// ============================================================
void Graphics::RestoreRenderTarget()
{
    if (m_InUIPhase)
        m_Context->OMSetRenderTargets(1, m_BackbufferRTV.GetAddressOf(), nullptr);
    else
        m_Context->OMSetRenderTargets(1, m_SceneRTV.GetAddressOf(), m_DepthStencilView.Get());
    m_Context->RSSetViewports(1, &m_Viewport);
}

// ============================================================
// リサイズ: 全部作り直す
// ============================================================
bool Graphics::Resize(int width, int height)
{
    if (!m_SwapChain || width <= 0 || height <= 0) return false;
    if ((float)width == m_Viewport.Width && (float)height == m_Viewport.Height)
        return true;

    m_Context->OMSetRenderTargets(0, nullptr, nullptr);
    m_BackbufferRTV.Reset();
    m_SceneRTV.Reset();
    m_SceneTex.Reset();
    m_SceneSRV.Reset();
    m_SceneResolved.Reset();
    m_DepthStencilView.Reset();

    HRESULT hr = m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
    {
        std::cout << "[Error] ResizeBuffers failed" << std::endl;
        return false;
    }

    if (!CreateSceneTargets(width, height)) return false;
    if (!m_Bloom.Resize(m_Device.Get(), width, height)) return false;
    m_Context->OMSetRenderTargets(1, m_SceneRTV.GetAddressOf(), m_DepthStencilView.Get());
    m_Context->RSSetViewports(1, &m_Viewport);

    std::cout << "[Graphics] resized: " << width << "x" << height << std::endl;
    return true;
}
