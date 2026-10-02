// ============================================================
// VFXLiquidRenderer.cpp
// ============================================================
#include "VFX_Editor/VFXLiquidRenderer.h"
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
    constexpr UINT kGrid = 16;                       // LiquidCommon.hlsli の LIQUID_GRID
    constexpr UINT kVertsPerLiquid = kGrid * kGrid * 6;

    bool MakeDynamicStructured(ID3D11Device* device, UINT stride, size_t count,
        Microsoft::WRL::ComPtr<ID3D11Buffer>& buf, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = (UINT)(stride * count);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = stride;
        Microsoft::WRL::ComPtr<ID3D11Buffer> b;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> s;
        if (FAILED(device->CreateBuffer(&bd, nullptr, &b))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = (UINT)count;
        if (FAILED(device->CreateShaderResourceView(b.Get(), &sd, &s))) return false;
        buf = b;
        srv = s;
        return true;
    }

    template <class T>
    bool Upload(ID3D11DeviceContext* ctx, ID3D11Buffer* buf, const std::vector<T>& src)
    {
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (FAILED(ctx->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
        std::copy(src.begin(), src.end(), static_cast<T*>(m.pData));
        ctx->Unmap(buf, 0);
        return true;
    }
}

bool VFXLiquidRenderer::Initialize(ID3D11Device* device)
{
    m_VS = ResourceManager::Get().LoadVS(L"VFXLiquidVS", L"Shader/VFX/VFXLiquidVS.hlsl");
    m_PS = ResourceManager::Get().LoadPS(L"VFXLiquidPS", L"Shader/VFX/VFXLiquidPS.hlsl");
    if (!m_VS || !m_PS) return false;
    if (!EnsureCapacity(device, 8)) return false;
    std::cout << "[OK] VFXLiquidRenderer initialized" << std::endl;
    return true;
}

bool VFXLiquidRenderer::EnsureCapacity(ID3D11Device* device, size_t count)
{
    if (count <= m_Capacity && m_ItemBuffer && m_DefBuffer) return true;
    const size_t cap = (std::max)({ count, m_Capacity * 2, (size_t)8 });
    if (!MakeDynamicStructured(device, sizeof(VFXLiquidInstance), cap, m_ItemBuffer, m_ItemSRV)) return false;
    if (!MakeDynamicStructured(device, sizeof(VFXLiquidDef), cap, m_DefBuffer, m_DefSRV)) return false;
    m_Capacity = cap;
    return true;
}

void VFXLiquidRenderer::Submit(const VFXLiquidInstance& inst, const VFXLiquidDef& def)
{
    VFXLiquidInstance i = inst;
    i.def = (uint32_t)m_Defs.size();
    m_Defs.push_back(def);
    m_Items.push_back(i);
}

void VFXLiquidRenderer::SetTerrain(ID3D11ShaderResourceView* heights, const Swarm::FrameCB& grid)
{
    m_Heights = heights;
    m_Grid = grid;
}

// ============================================================
// 描画。呼ぶ側は不透明物・群れの後、連番絵・粒子の前
// ============================================================
void VFXLiquidRenderer::Render(ID3D11DeviceContext* ctx, CameraBase* camera, const LightBuffer& light)
{
    if (m_Items.empty()) return;
    auto clear = [&]() { m_Items.clear(); m_Defs.clear(); };
    if (!ctx || !camera || !m_VS || !m_PS) { clear(); return; }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    ctx->GetDevice(&device);
    if (!EnsureCapacity(device.Get(), m_Items.size())) { clear(); return; }
    if (!Upload(ctx, m_ItemBuffer.Get(), m_Items) || !Upload(ctx, m_DefBuffer.Get(), m_Defs)) { clear(); return; }

    // 実時間（一時停止中も液面のゆらぎは止めなくてよい）
    static const auto t0 = std::chrono::steady_clock::now();

    CameraCB cam;
    cam.viewProj = camera->GetViewMatrix() * camera->GetProjectionMatrix();
    cam.hasTerrain = m_Heights ? 1u : 0u;
    FrameCB frame;
    frame.time = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    LightBuffer l = light;
    l.cameraPosition = camera->GetPosition();

    m_VS->Bind(ctx);
    m_PS->Bind(ctx);
    m_VS->WriteBuffer(ctx, 0, &cam);
    m_VS->WriteBuffer(ctx, 1, &m_Grid);   // 地形の格子（SwarmTerrainHeight）。地形が無ければ読まれない
    m_PS->WriteBuffer(ctx, 0, &l);
    m_PS->WriteBuffer(ctx, 1, &frame);

    ID3D11ShaderResourceView* vsSRV[4] = { m_ItemSRV.Get(), m_DefSRV.Get(), nullptr, m_Heights };
    ctx->VSSetShaderResources(0, 4, vsSRV);
    ID3D11ShaderResourceView* psSRV = m_DefSRV.Get();
    ctx->PSSetShaderResources(0, 1, &psSRV);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    auto& rs = RenderStates::Get();
    rs.ApplyAlphaBlend(ctx);   // 乗算済み alpha・両面
    ctx->OMSetDepthStencilState(rs.DepthReadOnly(), 0);

    ctx->DrawInstanced(kVertsPerLiquid, (UINT)m_Items.size(), 0, 0);

    ID3D11ShaderResourceView* nullSRV[4] = {};
    ctx->VSSetShaderResources(0, 4, nullSRV);
    ctx->PSSetShaderResources(0, 1, nullSRV);
    rs.Restore(ctx);
    clear();
}
