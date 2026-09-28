// ============================================================
// Graphics.cpp
// ============================================================
#include "Graphics/Graphics.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Graphics/Renderer/RenderStates.h"
#include <iostream>
#include <cstdlib>
#include <dxgi.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002   // Windows 10 1803+（古い SDK には無い）
#endif

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
    // D3D の調試層は既定で切（2026-09-28 用户決定。Debug 構成で 1 フレーム約 2.5 ms かかっていた）。
    // SRV / UAV の HAZARD などの警告を見たい時は VFXL_D3D_DEBUG=1（VS なら「デバッグ → 環境」に書く）
    char debugLayer[8] = {};
    if (GetEnvironmentVariableA("VFXL_D3D_DEBUG", debugLayer, sizeof(debugLayer)) > 0 && debugLayer[0] != '0')
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

    if (!CreateSwapChain(hWnd, width, height)) return false;

    // ---- 表示の同期の初期値 ----
    // TEMP-TEST: VFXL_NO_VSYNC で垂直同期を切る、VFXL_FPS_CAP=<数> で上限 fps（負荷の計測用）
    if (GetEnvironmentVariableA("VFXL_NO_VSYNC", nullptr, 0) > 0)
        m_Present.vsync = false;
    {
        char env[16] = {};
        if (GetEnvironmentVariableA("VFXL_FPS_CAP", env, sizeof(env)) > 0)
            m_Present.fpsCap = (float)atof(env);
    }
    // 上限 fps の待ち用。HIGH_RESOLUTION（Windows 10 1803+）が無ければ普通のタイマー
    m_FrameTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!m_FrameTimer)
        m_FrameTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    if (!CreateSceneTargets(width, height)) return false;
    if (!LoadPostShaders()) return false;
    if (!m_Bloom.Initialize(m_Device.Get(), width, height)) return false;
    m_Context->OMSetRenderTargets(1, m_SceneRTV.GetAddressOf(), m_DepthStencilView.Get());
    m_Context->RSSetViewports(1, &m_Viewport);

  
    
    std::cout << "[OK] Graphics initialized" << std::endl;
    return true;
}

// ============================================================
// swap chain
// flip 型（FLIP_DISCARD）+ 3 枚。blt 型の 1 枚だと、垂直同期中に 1 フレームが
// 1 リフレッシュ（165Hz なら 6.06 ms）を超えた途端に次の垂直同期まで止まり、半分の fps に落ちる。
// 3 枚あれば表示待ちの間も次を描けるので、実際の速さのまま出る。
// flip 型の backbuffer は MSAA にできない（場面は HDR RT 側で MSAA → resolve 済みなので困らない）。
// 撕裂の許可（ALLOW_TEARING）は垂直同期を切った時だけ Present で使う。
// 作れない環境では旧来の blt 型に戻す
// ============================================================
bool Graphics::CreateSwapChain(HWND hWnd, int width, int height)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    m_Device.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIFactory> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5)))
    {
        BOOL allow = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
            m_TearingSupported = allow == TRUE;
    }

    // TEMP-TEST: VFXL_BLT_SWAPCHAIN で旧来の blt 型に戻す（新旧の比較用）
    const bool forceBlt = GetEnvironmentVariableA("VFXL_BLT_SWAPCHAIN", nullptr, 0) > 0;
    ComPtr<IDXGIFactory2> factory2;
    if (!forceBlt && SUCCEEDED(factory.As(&factory2)))
    {
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = width;
        sd.Height = height;
        sd.Format = kBackbufferFormat;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 3;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Flags = m_TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> sc1;
        if (SUCCEEDED(factory2->CreateSwapChainForHwnd(m_Device.Get(), hWnd, &sd, nullptr, nullptr, &sc1))
            && SUCCEEDED(sc1.As(&m_SwapChain)))
        {
            m_FlipModel = true;
            m_SwapChainFlags = sd.Flags;
        }
    }

    if (!m_FlipModel)
    {
        m_TearingSupported = false;
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
        HRESULT hr = factory->CreateSwapChain(m_Device.Get(), &scd, &m_SwapChain);
        DX_CHECK(hr, "CreateSwapChain failed");
    }

    // 全画面は F11 の枠無し窓で行う。Alt+Enter の排他全画面は撕裂の許可と両立しないので切る
    factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

    std::cout << "[Graphics] swap chain: " << (m_FlipModel ? "flip discard x3" : "blt (fallback)")
        << ", tearing " << (m_TearingSupported ? "supported" : "not supported") << std::endl;
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
    m_CompositePS->WriteBuffer(m_Context.Get(), 0, &ccb);

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
    WaitForFrameCap();

    const bool tear = !m_Present.vsync && m_TearingSupported;
    const HRESULT hr = m_SwapChain->Present(m_Present.vsync ? 1 : 0, tear ? DXGI_PRESENT_ALLOW_TEARING : 0);
    static bool s_Reported = false;   // 毎フレーム出さない
    if (FAILED(hr) && !s_Reported)
    {
        std::cout << "[DX ERROR] Present failed (HRESULT: 0x" << std::hex << hr << std::dec << ")" << std::endl;
        s_Reported = true;
    }
}

// ============================================================
// 上限 fps: 前のフレームから 1/cap 秒経つまで待つ。
// 残り 2 ms 以上は waitable timer で寝て、最後は回して待つ（Sleep だけだと 1〜2 ms ずれる）。
// 目標時刻は「前の目標 + 周期」で進めてリズムを保つ。1 周期以上遅れたら今を基準に取り直す
// （遅れを取り返そうとして連続で出さない）
// ============================================================
void Graphics::WaitForFrameCap()
{
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);

    if (m_Present.fpsCap <= 0.0f)
    {
        m_LastFrameQpc = now.QuadPart;
        return;
    }

    const long long period = (long long)((double)freq.QuadPart / m_Present.fpsCap);
    const long long target = m_LastFrameQpc + period;
    if (m_LastFrameQpc == 0 || now.QuadPart - target > period)
    {
        m_LastFrameQpc = now.QuadPart;
        return;
    }

    for (;;)
    {
        QueryPerformanceCounter(&now);
        const long long remain = target - now.QuadPart;
        if (remain <= 0) break;
        const double remainMs = remain * 1000.0 / (double)freq.QuadPart;
        if (m_FrameTimer && remainMs > 2.0)
        {
            LARGE_INTEGER due;
            due.QuadPart = -(long long)((remainMs - 1.5) * 10000.0);   // 相対時間（100ns 単位、負）
            SetWaitableTimer(m_FrameTimer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(m_FrameTimer, INFINITE);
        }
        else
            YieldProcessor();
    }
    m_LastFrameQpc = target;
}

void Graphics::Shutdown()
{
    if (m_FrameTimer)
    {
        CloseHandle(m_FrameTimer);
        m_FrameTimer = nullptr;
    }
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

    HRESULT hr = m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, m_SwapChainFlags);
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
