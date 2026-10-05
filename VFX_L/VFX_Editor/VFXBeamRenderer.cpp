// ============================================================
// VFXBeamRenderer.cpp
// ============================================================
#include "VFX_Editor/VFXBeamRenderer.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Manager/ResourceManager.h"
#include "Camera/CameraBase.h"
#include <algorithm>
#include <chrono>
#include <iostream>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr UINT kSegments = 32;                   // VS と同じ（BEAM_SEGMENTS）
    constexpr UINT kVertsPerBeam = kSegments * 6;    // 分割ごとに四角 1 枚
    constexpr UINT kLayers = 3;                      // グロー / 主色 / 白芯
}

bool VFXBeamRenderer::Initialize(ID3D11Device* device)
{
    m_VS = ResourceManager::Get().LoadVS(L"VFXBeamVS", L"Shader/VFX/VFXBeamVS.hlsl");
    m_PS = ResourceManager::Get().LoadPS(L"VFXBeamPS", L"Shader/VFX/VFXBeamPS.hlsl");
    if (!m_VS || !m_PS) return false;
    if (!EnsureCapacity(device, 16)) return false;
    std::cout << "[OK] VFXBeamRenderer initialized" << std::endl;
    return true;
}

bool VFXBeamRenderer::EnsureCapacity(ID3D11Device* device, size_t count)
{
    if (count <= m_Capacity && m_ItemBuffer) return true;
    const size_t cap = (std::max)({ count, m_Capacity * 2, (size_t)16 });

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sizeof(VFXBeamItem) * cap);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(VFXBeamItem);

    Microsoft::WRL::ComPtr<ID3D11Buffer> buf;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &buf))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = (UINT)cap;
    if (FAILED(device->CreateShaderResourceView(buf.Get(), &sd, &srv))) return false;

    m_ItemBuffer = buf;
    m_ItemSRV = srv;
    m_Capacity = cap;
    return true;
}

// ============================================================
// 描画。呼ぶ側は不透明物の後、粒子の前（Sprite entry と同じ所）
// ============================================================
void VFXBeamRenderer::Render(ID3D11DeviceContext* ctx, CameraBase* camera)
{
    if (m_Items.empty()) return;
    if (!ctx || !camera || !m_VS || !m_PS) { m_Items.clear(); return; }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    ctx->GetDevice(&device);
    if (!EnsureCapacity(device.Get(), m_Items.size())) { m_Items.clear(); return; }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(ctx->Map(m_ItemBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { m_Items.clear(); return; }
    std::copy(m_Items.begin(), m_Items.end(), static_cast<VFXBeamItem*>(mapped.pData));
    ctx->Unmap(m_ItemBuffer.Get(), 0);

    // 実時間（一時停止中も光線の揺れは止めなくてよい）
    static const auto t0 = std::chrono::steady_clock::now();
    BeamCB cb;
    cb.viewProj = camera->GetViewMatrix() * camera->GetProjectionMatrix();
    cb.camPos = camera->GetPosition();
    cb.first = 0;
    cb.time = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();

    m_VS->Bind(ctx);
    m_PS->Bind(ctx);
    m_VS->WriteBuffer(ctx, 0, &cb);
    ID3D11ShaderResourceView* items = m_ItemSRV.Get();
    ctx->VSSetShaderResources(0, 1, &items);
    ctx->PSSetShaderResources(0, 1, &items);   // PS も本ごとの色・ノイズを読む
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    RenderStates::Get().ApplyAlphaBlend(ctx);   // 乗算済み alpha・深度は読むだけ・両面。PS は alpha 0 = 加算
    ctx->OMSetDepthStencilState(RenderStates::Get().DepthReadOnly(), 0);

    ctx->DrawInstanced(kVertsPerBeam, (UINT)(m_Items.size() * kLayers), 0, 0);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->VSSetShaderResources(0, 1, &nullSRV);
    ctx->PSSetShaderResources(0, 1, &nullSRV);
    RenderStates::Get().Restore(ctx);
    m_Items.clear();
}
