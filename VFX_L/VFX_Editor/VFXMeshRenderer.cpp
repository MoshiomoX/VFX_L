// ============================================================
// VFXMeshRenderer.cpp
// ============================================================
#include "VFX_Editor/VFXMeshRenderer.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Manager/ResourceManager.h"
#include "Camera/CameraBase.h"
#include <algorithm>
#include <iostream>

using namespace DirectX::SimpleMath;

bool VFXMeshRenderer::Initialize(ID3D11Device* device)
{
    m_VS = ResourceManager::Get().LoadVS(L"VFXMeshVS", L"Shader/VFX/VFXMeshVS.hlsl");
    m_PS = ResourceManager::Get().LoadPS(L"VFXMeshPS", L"Shader/VFX/VFXMeshPS.hlsl");
    if (!m_VS || !m_PS) return false;

    m_White = std::make_shared<Texture>();
    m_White->CreateSolid(device, 255, 255, 255, 255);

    // ---- blend ----
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&bd, &m_BlendAdditive))) return false;

    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    if (FAILED(device->CreateBlendState(&bd, &m_BlendAlpha))) return false;

    // ---- depth: 読むだけ ----
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(device->CreateDepthStencilState(&dd, &m_DepthReadOnly))) return false;

    // ---- raster ----
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rd, &m_RasterCullBack))) return false;
    rd.CullMode = D3D11_CULL_NONE;
    if (FAILED(device->CreateRasterizerState(&rd, &m_RasterCullNone))) return false;

    std::cout << "[OK] VFXMeshRenderer initialized" << std::endl;
    return true;
}

// ============================================================
// 描画。呼ぶ側は不透明物の後、粒子の前
// ============================================================
void VFXMeshRenderer::Render(ID3D11DeviceContext* ctx, CameraBase* camera)
{
    if (m_Items.empty() || !camera || !m_VS || !m_PS) return;

    const Matrix view = camera->GetViewMatrix();
    const Matrix proj = camera->GetProjectionMatrix();
    const Vector3 camPos = camera->GetPosition();

    // alpha は遠 → 近。additive は順不同なので後ろにまとめる
    std::vector<const VFXMeshDrawItem*> alpha, additive;
    for (auto& it : m_Items)
        (it.blend == 1 ? alpha : additive).push_back(&it);

    std::sort(alpha.begin(), alpha.end(),
        [&](const VFXMeshDrawItem* a, const VFXMeshDrawItem* b)
        {
            return Vector3::DistanceSquared(a->world.Translation(), camPos)
                 > Vector3::DistanceSquared(b->world.Translation(), camPos);
        });

    m_VS->Bind(ctx);
    m_PS->Bind(ctx);
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    ctx->PSSetSamplers(0, 1, &samp);
    ctx->OMSetDepthStencilState(m_DepthReadOnly.Get(), 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const float bf[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(m_BlendAlpha.Get(), bf, 0xFFFFFFFF);
    for (auto* it : alpha) Draw(ctx, *it, view, proj);

    ctx->OMSetBlendState(m_BlendAdditive.Get(), bf, 0xFFFFFFFF);
    for (auto* it : additive) Draw(ctx, *it, view, proj);

    ID3D11ShaderResourceView* nulls[3] = {};
    ctx->PSSetShaderResources(0, 3, nulls);
    RenderStates::Get().Restore(ctx);

    m_Items.clear();
}

void VFXMeshRenderer::Draw(ID3D11DeviceContext* ctx, const VFXMeshDrawItem& item,
    const Matrix& view, const Matrix& proj)
{
    if (!item.model) return;

    // ModelCommon.hlsli の MVPBuffer と同じ並び（row_major、転置しない）
    struct { Matrix W, V, P; } mvp{ item.world, view, proj };
    m_VS->WriteBuffer(ctx, 0, (void*)&mvp);

    VFXMeshParams params = item.params;
    m_PS->WriteBuffer(ctx, 1, &params);

    ID3D11ShaderResourceView* srvs[3] = {
        (item.mainTex ? item.mainTex : m_White.get())->GetSRV(),
        item.noiseTex ? item.noiseTex->GetSRV() : nullptr,
        item.maskTex ? item.maskTex->GetSRV() : nullptr,
    };
    ctx->PSSetShaderResources(0, 3, srvs);
    ctx->RSSetState(item.twoSided ? m_RasterCullNone.Get() : m_RasterCullBack.Get());

    for (const auto& sub : item.model->GetSubMeshes())
        if (sub.mesh) sub.mesh->Draw(ctx);
}