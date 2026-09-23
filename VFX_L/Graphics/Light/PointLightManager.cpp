// ============================================================
// PointLightManager.cpp
// ============================================================
#include "Graphics/Light/PointLightManager.h"
#include <iostream>

namespace
{
    // DEFAULT + UAV の structured buffer（CS が追記し、PS が SRV で読む）
    bool MakeStructured(ID3D11Device* device, UINT stride, UINT count,
        Microsoft::WRL::ComPtr<ID3D11Buffer>& buf,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv,
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& uav)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = stride * count;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = stride;
        if (FAILED(device->CreateBuffer(&bd, nullptr, &buf))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = count;
        if (FAILED(device->CreateShaderResourceView(buf.Get(), &sd, &srv))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = count;
        if (FAILED(device->CreateUnorderedAccessView(buf.Get(), &ud, &uav))) return false;
        return true;
    }
}

bool PointLightManager::Initialize(ID3D11Device* device)
{
    if (!device) return false;
    if (!MakeStructured(device, sizeof(PointLight), kMaxLights, m_LightBuffer, m_LightSRV, m_LightUAV))
    {
        std::cout << "[PointLight] light buffer create failed" << std::endl;
        return false;
    }
    if (!MakeStructured(device, sizeof(uint32_t), 1, m_CountBuffer, m_CountSRV, m_CountUAV))
    {
        std::cout << "[PointLight] count buffer create failed" << std::endl;
        return false;
    }
    m_Cpu.reserve(kMaxLights);
    std::cout << "[OK] PointLightManager: " << kMaxLights << " lights" << std::endl;
    return true;
}

void PointLightManager::Shutdown()
{
    m_Cpu.clear();
    m_LightUAV.Reset(); m_LightSRV.Reset(); m_LightBuffer.Reset();
    m_CountUAV.Reset(); m_CountSRV.Reset(); m_CountBuffer.Reset();
}

void PointLightManager::BeginFrame()
{
    m_Cpu.clear();
    m_Uploaded = false;
}

void PointLightManager::Add(const PointLight& light)
{
    if (m_Cpu.size() >= kMaxLights) return;   // 先着順。超えた分は捨てる
    if (light.intensity <= 0.0f || light.radius <= 0.0f) return;
    m_Cpu.push_back(light);
}

void PointLightManager::Upload(ID3D11DeviceContext* ctx)
{
    if (m_Uploaded || !ctx || !m_LightBuffer) return;
    m_Uploaded = true;

    const uint32_t n = (uint32_t)m_Cpu.size();
    if (n > 0)
    {
        D3D11_BOX box = {};
        box.right = n * sizeof(PointLight);
        box.bottom = 1; box.back = 1;
        ctx->UpdateSubresource(m_LightBuffer.Get(), 0, &box, m_Cpu.data(), 0, 0);
    }
    ctx->UpdateSubresource(m_CountBuffer.Get(), 0, nullptr, &n, 0, 0);
}

void PointLightManager::BindPS(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;
    ID3D11ShaderResourceView* srvs[2] = { m_LightSRV.Get(), m_CountSRV.Get() };
    ctx->PSSetShaderResources(kLightSlot, 2, srvs);
}

void PointLightManager::UnbindPS(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;
    ID3D11ShaderResourceView* nulls[2] = { nullptr, nullptr };
    ctx->PSSetShaderResources(kLightSlot, 2, nulls);
}
