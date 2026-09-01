#include "Graphics/Graphics.h"
#include <iostream>
#include <dxgi.h>
#include "Debug/ImGuiManager.h"

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

    // 1. ??Device????(MSAA???)
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION,
        &m_Device, nullptr, &m_Context
    );
    DX_CHECK(hr, "D3D11CreateDevice failed");

    // 2. MSAA????
    UINT msaaQuality = 0;


    m_Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &msaaQuality);

    UINT sampleCount = (msaaQuality > 0) ? 4 : 1;
    UINT sampleQuality = (msaaQuality > 0) ? msaaQuality - 1 : 0;

    std::cout << "[Info] MSAA: " << sampleCount << "x (Quality: " << sampleQuality << ")" << std::endl;

    // 3. Swap
    // ??
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

    // 4. RenderTargetView??
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    DX_CHECK(hr, "GetBuffer failed");

    hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_RenderTargetView);
    DX_CHECK(hr, "CreateRenderTargetView failed");

    // 5. DepthStencil??(MSAA?????)
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = sampleCount;      // MSAA???
    depthDesc.SampleDesc.Quality = sampleQuality;  // MSAA???
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depthBuffer;
    hr = m_Device->CreateTexture2D(&depthDesc, nullptr, &depthBuffer);
    DX_CHECK(hr, "CreateTexture2D failed");

    hr = m_Device->CreateDepthStencilView(depthBuffer.Get(), nullptr, &m_DepthStencilView);
    DX_CHECK(hr, "CreateDepthStencilView failed");

    // 6. Viewport??
    m_Viewport.Width = static_cast<FLOAT>(width);
    m_Viewport.Height = static_cast<FLOAT>(height);
    m_Viewport.MinDepth = 0.0f;
    m_Viewport.MaxDepth = 1.0f;
    m_Viewport.TopLeftX = 0.0f;
    m_Viewport.TopLeftY = 0.0f;

    m_Context->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), m_DepthStencilView.Get());
    m_Context->RSSetViewports(1, &m_Viewport);

    std::cout << "[OK] Graphics initialized" << std::endl;  
    return true;
}


void Graphics::BeginFrame()
{
	// ????????????
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
	// ?????????(ComPtr???????????)
}
void Graphics::RestoreRenderTarget()
{
    m_Context->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), m_DepthStencilView.Get());
    m_Context->RSSetViewports(1, &m_Viewport);
}
bool Graphics::Resize(int width, int height)
{
    if (!m_SwapChain || width <= 0 || height <= 0) return false;
    if ((float)width == m_Viewport.Width && (float)height == m_Viewport.Height)
        return true;   // ????

    // ---- ?????????????(ResizeBuffers ???)----
    m_Context->OMSetRenderTargets(0, nullptr, nullptr);
    m_RenderTargetView.Reset();
    m_DepthStencilView.Reset();

    // ---- ???????????? ----
    HRESULT hr = m_SwapChain->ResizeBuffers(
        0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
    {
        std::cout << "[Error] ResizeBuffers failed" << std::endl;
        return false;
    }

    // ---- RenderTargetView ????? ----
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_RenderTargetView);
    if (FAILED(hr)) return false;

    // ---- DepthStencil ?????(MSAA ??? swap chain ??????)----
    DXGI_SWAP_CHAIN_DESC scd = {};
    m_SwapChain->GetDesc(&scd);

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = scd.SampleDesc.Count;      // MSAA ????
    depthDesc.SampleDesc.Quality = scd.SampleDesc.Quality;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depthBuffer;
    hr = m_Device->CreateTexture2D(&depthDesc, nullptr, &depthBuffer);
    if (FAILED(hr)) return false;

    hr = m_Device->CreateDepthStencilView(depthBuffer.Get(), nullptr, &m_DepthStencilView);
    if (FAILED(hr)) return false;

    // ---- ????????(GetWidth/GetHeight ???????)----
    m_Viewport.Width = (float)width;
    m_Viewport.Height = (float)height;
    m_Viewport.TopLeftX = 0.0f;
    m_Viewport.TopLeftY = 0.0f;
    m_Viewport.MinDepth = 0.0f;
    m_Viewport.MaxDepth = 1.0f;

    m_Context->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(),
        m_DepthStencilView.Get());
    m_Context->RSSetViewports(1, &m_Viewport);

    std::cout << "[Graphics] resized: " << width << "x" << height << std::endl;
    return true;
}