// ============================================================
// RenderStates.cpp
// ============================================================
#include "Graphics/Renderer/RenderStates.h"
#include <iostream>

bool RenderStates::Initialize(ID3D11Device* device)
{
    if (m_Initialized) return true;
    if (!device) return false;

    m_Common = std::make_unique<DirectX::CommonStates>(device);

    // --- Skybox 用：球の内側を描くため深度は LESS_EQUAL、書き込みあり ---
    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(device->CreateDepthStencilState(&ds, &m_DepthLessEqual)))
        return false;

    // --- 左手系 + CW 巻き順の前提でカリングを定義する ---
    //   CommonStates の CullClockwise/CounterClockwise は
    //   巻き順の解釈が紛らわしいため、明示的に自前で作る。
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;

    rd.CullMode = D3D11_CULL_BACK;
    if (FAILED(device->CreateRasterizerState(&rd, &m_CullBack))) return false;

    rd.CullMode = D3D11_CULL_FRONT;
    if (FAILED(device->CreateRasterizerState(&rd, &m_CullFront))) return false;

    m_Initialized = true;
    std::cout << "[OK] RenderStates initialized" << std::endl;
    return true;
}

void RenderStates::Shutdown()
{
    m_Common.reset();
    m_DepthLessEqual.Reset();
    m_CullBack.Reset();
    m_CullFront.Reset();
    m_Initialized = false;
}

// ============================================================
// 用途別プリセット
// ============================================================
namespace
{
    const float kBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
}

void RenderStates::ApplyOpaque(ID3D11DeviceContext* ctx) const
{
    if (!ctx || !m_Initialized) return;
    ctx->OMSetBlendState(Opaque(), kBlendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(DepthDefault(), 0);
    ctx->RSSetState(CullBack());
}

void RenderStates::ApplyAdditiveBillboard(ID3D11DeviceContext* ctx) const
{
    if (!ctx || !m_Initialized) return;
    ctx->OMSetBlendState(Additive(), kBlendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(DepthReadOnly(), 0);   // 深度は読むが書かない
    ctx->RSSetState(CullNone());                       // 板なので両面描く
}

void RenderStates::ApplyAlphaBlend(ID3D11DeviceContext* ctx) const
{
    if (!ctx || !m_Initialized) return;
    ctx->OMSetBlendState(AlphaBlend(), kBlendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(DepthReadOnly(), 0);
    ctx->RSSetState(CullNone());
}

void RenderStates::ApplySkybox(ID3D11DeviceContext* ctx) const
{
    if (!ctx || !m_Initialized) return;
    ctx->OMSetBlendState(Opaque(), kBlendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(DepthLessEqual(), 0);
    ctx->RSSetState(CullFront());
}

void RenderStates::ApplyUI(ID3D11DeviceContext* ctx) const
{
    if (!ctx || !m_Initialized) return;
    ctx->OMSetBlendState(AlphaBlend(), kBlendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(DepthNone(), 0);  
    ctx->RSSetState(CullNone());
}
// ============================================================
// 既定状態へ戻す
// ※各描画パスの末尾で必ず呼ぶ。忘れると次のパスが壊れる。
// ============================================================
void RenderStates::Restore(ID3D11DeviceContext* ctx) const
{
    if (!ctx || !m_Initialized) return;
    ctx->OMSetBlendState(Opaque(), kBlendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(DepthDefault(), 0);
    ctx->RSSetState(CullBack());
}