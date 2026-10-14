#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <dxgi1_5.h>
#include "Graphics/PostProcess/Bloom.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

class VertexShader;
class PixelShader;

// ============================================================
// Graphics
// シーンは HDR のオフスクリーン RT（R16G16B16A16_FLOAT, MSAA）へ描く。
// BeginUI で resolve → 合成 PS で backbuffer へ。UI はその後に描く。
//
// フレームの流れ:
//   BeginFrame  … HDR RT を clear して bind
//   (シーン描画)
//   BeginUI     … resolve → 合成 → backbuffer を bind
//   (UI / ImGui 描画)
//   EndFrame    … (上限 fps の待ち) → Present
//
// swap chain は flip 型（FLIP_DISCARD、3 枚、backbuffer は MSAA 無し）。
// 垂直同期を切った時は ALLOW_TEARING で即表示する（可変リフレッシュの画面ならこれで撕れない）
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

    // 今の段階に合った RT に戻す（シーン中なら HDR RT、UI 中なら backbuffer）
    void RestoreRenderTarget();

    // resolve 済みの HDR シーン（後処理の入力。BeginUI 以降で有効）
    ID3D11ShaderResourceView* GetSceneSRV() const { return m_SceneSRV.Get(); }

    // シーンの深度（MSAA なら Texture2DMS）と、シーンの RT / DSV（2026-10-04、画面のアウトラインが深度を読む）。
    // 深度を SRV で読む間は DSV を外すこと（同じ資源の読み書き）。読み終えたら RestoreRenderTarget
    ID3D11ShaderResourceView* GetDepthSRV() const { return m_DepthSRV.Get(); }
    // 同じ深度のステンシル（G チャンネル、uint）。陣営のアウトライン（2026-10-06）。使い方は深度と同じ
    ID3D11ShaderResourceView* GetStencilSRV() const { return m_StencilSRV.Get(); }
    ID3D11RenderTargetView* GetSceneRTV() const { return m_SceneRTV.Get(); }
    UINT GetSampleCount() const { return m_SampleCount; }

    float GetWidth()  const { return m_Viewport.Width; }
    float GetHeight() const { return m_Viewport.Height; }
    float GetAspect() const { return m_Viewport.Width / m_Viewport.Height; }
    BloomParams& GetBloomParams() { return m_Bloom.Params(); }

    // ---- 表示の同期（Debug Info 欄で切り替える）----
    struct PresentSettings
    {
        bool  vsync = true;     // 切るとティアリングを許して即表示（ALLOW_TEARING が使える時）
        float fpsCap = 0.0f;    // 上限 fps。0 = 無し。Present の直前に高精度タイマーで待つ
    };
    PresentSettings& GetPresentSettings() { return m_Present; }
    bool IsTearingSupported() const { return m_TearingSupported; }
    bool IsFlipModel() const { return m_FlipModel; }
private:
    bool CreateSceneTargets(int width, int height);
    bool LoadPostShaders();
    bool CreateSwapChain(HWND hWnd, int width, int height);
    void WaitForFrameCap();

    Bloom m_Bloom;
    ComPtr<ID3D11Device> m_Device;
    ComPtr<ID3D11DeviceContext> m_Context;
    ComPtr<IDXGISwapChain> m_SwapChain;

    // ---- backbuffer（UI と合成結果の行き先）----
    ComPtr<ID3D11RenderTargetView> m_BackbufferRTV;

    // ---- シーン用 HDR RT（MSAA）と resolve 先（非 MSAA, SRV）----
    ComPtr<ID3D11Texture2D> m_SceneTex;
    ComPtr<ID3D11RenderTargetView> m_SceneRTV;
    ComPtr<ID3D11Texture2D> m_SceneResolved;
    ComPtr<ID3D11ShaderResourceView> m_SceneSRV;
    ComPtr<ID3D11DepthStencilView> m_DepthStencilView;
    ComPtr<ID3D11ShaderResourceView> m_DepthSRV;   // 同じ深度を読む（画面のアウトライン）
    ComPtr<ID3D11ShaderResourceView> m_StencilSRV; // 同じ資源のステンシル（陣営のアウトライン）

    // ---- 合成（全画面三角形）----
    std::shared_ptr<VertexShader> m_CompositeVS;
    std::shared_ptr<PixelShader> m_CompositePS;

    // ---- 表示の同期 ----
    PresentSettings m_Present;
    bool m_FlipModel = false;          // 作れなかった時は旧来の blt 型（MSAA backbuffer）
    bool m_TearingSupported = false;
    UINT m_SwapChainFlags = 0;         // ResizeBuffers にも同じ値を渡す
    HANDLE m_FrameTimer = nullptr;     // 上限 fps の待ち（高精度 waitable timer）
    long long m_LastFrameQpc = 0;      // 前のフレームを出した時刻（QPC）

    UINT m_SampleCount = 1;
    UINT m_SampleQuality = 0;
    bool m_InUIPhase = false;

    D3D11_VIEWPORT m_Viewport = {};
};