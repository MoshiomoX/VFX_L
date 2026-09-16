#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include "Graphics/PostProcess/Bloom.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

class VertexShader;
class PixelShader;

// ============================================================
// Graphics
// 場面は HDR の離屏 RT（R16G16B16A16_FLOAT, MSAA）へ描く。
// BeginUI で resolve → 合成 PS で backbuffer へ。UI はその後に描く。
//
// フレームの流れ:
//   BeginFrame  … HDR RT を clear して bind
//   (場面描画)
//   BeginUI     … resolve → 合成 → backbuffer を bind
//   (UI / ImGui 描画)
//   EndFrame    … Present
// ============================================================
class Graphics
{
public:


    bool Initialize(HWND hWnd, int width, int height);
    void BeginFrame();
    void BeginUI();
    void EndFrame();
    void Shutdown();
    bool Resize(int width, int height);

    ID3D11Device* GetDevice() const { return m_Device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_Context.Get(); }

    // 今の段階に合った RT に戻す（場面中なら HDR RT、UI 中なら backbuffer）
    void RestoreRenderTarget();

    // resolve 済みの HDR 場面（後処理の入力。BeginUI 以降で有効）
    ID3D11ShaderResourceView* GetSceneSRV() const { return m_SceneSRV.Get(); }

    float GetWidth()  const { return m_Viewport.Width; }
    float GetHeight() const { return m_Viewport.Height; }
    float GetAspect() const { return m_Viewport.Width / m_Viewport.Height; }
    BloomParams& GetBloomParams() { return m_Bloom.Params(); }
private:
    bool CreateSceneTargets(int width, int height);
    bool LoadPostShaders();

    Bloom m_Bloom;
    ComPtr<ID3D11Device> m_Device;
    ComPtr<ID3D11DeviceContext> m_Context;
    ComPtr<IDXGISwapChain> m_SwapChain;

    // ---- backbuffer（UI と合成結果の行き先）----
    ComPtr<ID3D11RenderTargetView> m_BackbufferRTV;

    // ---- 場面用 HDR RT（MSAA）と resolve 先（非 MSAA, SRV）----
    ComPtr<ID3D11Texture2D> m_SceneTex;
    ComPtr<ID3D11RenderTargetView> m_SceneRTV;
    ComPtr<ID3D11Texture2D> m_SceneResolved;
    ComPtr<ID3D11ShaderResourceView> m_SceneSRV;
    ComPtr<ID3D11DepthStencilView> m_DepthStencilView;

    // ---- 合成（全画面三角形）----
    std::shared_ptr<VertexShader> m_CompositeVS;
    std::shared_ptr<PixelShader> m_CompositePS;

    UINT m_SampleCount = 1;
    UINT m_SampleQuality = 0;
    bool m_InUIPhase = false;

    D3D11_VIEWPORT m_Viewport = {};
};