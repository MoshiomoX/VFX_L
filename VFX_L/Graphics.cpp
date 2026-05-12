#include "Graphics.h"
#include <iostream>
#include <dxgi.h>
#include "ImGuiManager.h"

#define DX_CHECK(hr, msg) \
    if (FAILED(hr)) { \
        std::cout << "[DX ERROR] " << msg << " (HRESULT: 0x" << std::hex << hr << ")" << std::endl; \
        return false; \
    }


bool Graphics::Initialize(HWND hWnd, int width, int height)
{
    std::cout << "[Debug] hwnd: " << hWnd << std::endl;
    if (!IsWindow(hWnd))
    {
        std::cout << "[ERROR] Invalid HWND" << std::endl;
        return false;
    }
    std::cout << "[Debug] width: " << width << ", height: " << height << std::endl;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // 1. まずDeviceだけ作成（MSAA確認用）
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION,
        &m_Device, nullptr, &m_Context
    );
    DX_CHECK(hr, "D3D11CreateDevice failed");

    // 2. MSAA対応確認
    UINT msaaQuality = 0;


    m_Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &msaaQuality);

    UINT sampleCount = (msaaQuality > 0) ? 4 : 1;
    UINT sampleQuality = (msaaQuality > 0) ? msaaQuality - 1 : 0;

    std::cout << "[Info] MSAA: " << sampleCount << "x (Quality: " << sampleQuality << ")" << std::endl;

    // 3. SwapChain作成
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
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWnd;
    scd.SampleDesc.Count = sampleCount;
    scd.SampleDesc.Quality = sampleQuality;
    scd.Windowed = TRUE;

    hr = factory->CreateSwapChain(m_Device.Get(), &scd, &m_SwapChain);
    DX_CHECK(hr, "CreateSwapChain failed");

    // 4. RenderTargetView作成
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    DX_CHECK(hr, "GetBuffer failed");

    hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_RenderTargetView);
    DX_CHECK(hr, "CreateRenderTargetView failed");

    // 5. DepthStencil作成（MSAAと同じ設定）
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = sampleCount;      // MSAAと一致
    depthDesc.SampleDesc.Quality = sampleQuality;  // MSAAと一致
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depthBuffer;
    hr = m_Device->CreateTexture2D(&depthDesc, nullptr, &depthBuffer);
    DX_CHECK(hr, "CreateTexture2D failed");

    hr = m_Device->CreateDepthStencilView(depthBuffer.Get(), nullptr, &m_DepthStencilView);
    DX_CHECK(hr, "CreateDepthStencilView failed");

    // 6. Viewport設定
    m_Viewport.Width = static_cast<FLOAT>(width);
    m_Viewport.Height = static_cast<FLOAT>(height);
    m_Viewport.MinDepth = 0.0f;
    m_Viewport.MaxDepth = 1.0f;
    m_Viewport.TopLeftX = 0.0f;
    m_Viewport.TopLeftY = 0.0f;

    m_Context->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), m_DepthStencilView.Get());
    m_Context->RSSetViewports(1, &m_Viewport);

    std::cout << "[OK] Graphics initialized" << std::endl;



    // Rasterizer State
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;

    ComPtr<ID3D11RasterizerState> rs;
    m_Device->CreateRasterizerState(&rd, &rs);
    m_Context->RSSetState(rs.Get());
    
    return true;
}


void Graphics::BeginFrame()
{
	// 背景色で画面全体をクリア
	const float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };// RGBA
	m_Context->ClearRenderTargetView(m_RenderTargetView.Get(), clearColor);
	m_Context->ClearDepthStencilView(m_DepthStencilView.Get(), D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void Graphics::EndFrame()
{
	
	m_SwapChain->Present(1, 0);
}

void Graphics::Shutdown()
{
	// 特に解放処理は不要（ComPtrが自動で解放してくれる）
}