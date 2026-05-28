#include "GPUParticleSystem.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "d3dcompiler.lib")

bool GPUParticleSystem::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t maxParticles)
{
    m_Device = device;
    m_Context = context;
    m_MaxParticles = maxParticles;

    if (!CreateParticleBuffer(device))   return false;
    if (!CreateEmitterBuffer(device))    return false;
    if (!m_DeadList.Initialize(device, maxParticles)) return false;
    if (!LoadShaders(device))            return false;
    if (!CreateRenderStates(device))     return false;
    if (!CreateColorKeyBuffer(device))   return false;

    // Dead Listを全インデックスで初期化
    DeadListCB dlcb = {};
    dlcb.deadCount = 0;
    dlcb.maxParticles = maxParticles;

    m_InitDeadListCS->WriteBuffer(context, 0, &dlcb);
    m_InitDeadListCS->Bind(context);
    m_DeadList.BindCSUAV(context, 0, 0);  // Bindの後

    context->Dispatch((maxParticles + 255) / 256, 1, 1);

    m_DeadList.UnbindCSUAV(context, 0);
    m_InitDeadListCS->Unbind(context);

    m_CurrentDeadCount = m_DeadList.ReadDeadCount(context);

    return true;
}

bool GPUParticleSystem::CreateParticleBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(GPUParticle) * m_MaxParticles;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(GPUParticle);

    HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_ParticleBuffer);
    if (FAILED(hr)) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = m_MaxParticles;

    hr = device->CreateUnorderedAccessView(m_ParticleBuffer.Get(), &uavDesc, &m_ParticleUAV);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = m_MaxParticles;

    hr = device->CreateShaderResourceView(m_ParticleBuffer.Get(), &srvDesc, &m_ParticleSRV);
    if (FAILED(hr)) return false;

    return true;
}

bool GPUParticleSystem::CreateColorKeyBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(ColorKey) * MAX_COLOR_KEYS_TOTAL;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(ColorKey);

    HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_ColorKeyBuffer);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MAX_COLOR_KEYS_TOTAL;

    hr = device->CreateShaderResourceView(m_ColorKeyBuffer.Get(), &srvDesc, &m_ColorKeySRV);
    if (FAILED(hr)) return false;

    return true;
}

bool GPUParticleSystem::CreateEmitterBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(GPUEmitter) * MAX_EMITTERS;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(GPUEmitter);

    HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_EmitterBuffer);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MAX_EMITTERS;

    hr = device->CreateShaderResourceView(m_EmitterBuffer.Get(), &srvDesc, &m_EmitterSRV);
    if (FAILED(hr)) return false;

    return true;
}

bool GPUParticleSystem::LoadShaders(ID3D11Device* device)
{
    m_InitDeadListCS = std::make_shared<ComputeShader>();
    HRESULT hr = m_InitDeadListCS->Compile(device, L"Shader/Particle/InitDeadListCS.hlsl");
    std::cout << "[LoadShaders] InitDeadListCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << " hr=0x" << std::hex << hr << std::dec << std::endl;
    if (FAILED(hr)) return false;

    m_EmitCS = std::make_shared<ComputeShader>();
    hr = m_EmitCS->Compile(device, L"Shader/Particle/ParticleEmitCS.hlsl");
    std::cout << "[LoadShaders] EmitCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_UpdateCS = std::make_shared<ComputeShader>();
    hr = m_UpdateCS->Compile(device, L"Shader/Particle/ParticleUpdateCS.hlsl");
    std::cout << "[LoadShaders] UpdateCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_RenderVS = std::make_shared<VertexShader>();
    hr = m_RenderVS->Compile(device, L"Shader/Particle/GPUParticleVS.hlsl");
    std::cout << "[LoadShaders] RenderVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << " hr=0x" << std::hex << hr << std::dec << std::endl;
    if (FAILED(hr)) return false;

    m_RenderPS = std::make_shared<PixelShader>();
    hr = m_RenderPS->Compile(device, L"Shader/Particle/GPUParticlePS.hlsl");
    std::cout << "[LoadShaders] RenderPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    return true;
}

bool GPUParticleSystem::CreateRenderStates(ID3D11Device* device)
{
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    HRESULT hr = device->CreateBlendState(&blendDesc, &m_BlendState);
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;

    hr = device->CreateDepthStencilState(&dsDesc, &m_DepthStencilState);
    if (FAILED(hr)) return false;

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;

    hr = device->CreateRasterizerState(&rsDesc, &m_RasterizerState);
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    hr = device->CreateSamplerState(&sampDesc, &m_SamplerState);
    if (FAILED(hr)) return false;

    return true;
}

void GPUParticleSystem::Update(float deltaTime, float totalTime,
    const std::vector<GPUEmitter>& emitters,
    const std::vector<ColorKey>& colorKeys)
{
    auto context = m_Context;

    m_CurrentDeadCount = m_DeadList.ReadDeadCount(context);

    m_CachedGlobalCB = {};
    m_CachedGlobalCB.deltaTime = deltaTime;
    m_CachedGlobalCB.totalTime = totalTime;
    m_CachedGlobalCB.baseSeed = static_cast<uint32_t>(totalTime * 1000.0f);
    m_CachedGlobalCB.emitterCount = static_cast<int>(emitters.size());

    // 外部emitterから合計発射数を計算してキャッシュ
    uint32_t totalEmit = 0;
    for (auto& e : emitters)
    {
        if (e.isActive > 0.5f)
            totalEmit += e.emitCount;
    }


    UploadExternalEmitters(context, emitters, colorKeys);
    DispatchEmit(context,totalEmit);
    DispatchUpdate(context);
}
void GPUParticleSystem::UploadEmitters(ID3D11DeviceContext* context)
{
    if (m_Emitters.empty()) return;

    int colorOffset = 0;
    for (size_t i = 0; i < m_Emitters.size(); ++i)
    {
        m_Emitters[i]->SetColorKeyOffset(colorOffset);
        colorOffset += m_Emitters[i]->colorKeyCount;
    }

    // Emitterアップロード（溢れチェック付き）
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(m_EmitterBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    size_t emitterCount = (std::min)(m_Emitters.size(), static_cast<size_t>(MAX_EMITTERS));

    std::vector<GPUEmitter> emitterData(emitterCount);
    for (size_t i = 0; i < emitterCount; ++i)
    {
        emitterData[i] = m_Emitters[i]->ToGPU();
    }

    memcpy(mapped.pData, emitterData.data(), sizeof(GPUEmitter) * emitterCount);
    context->Unmap(m_EmitterBuffer.Get(), 0);

    // ColorKeyアップロード（溢れチェック付き）
    std::vector<ColorKey> allColorKeys;
    for (size_t i = 0; i < emitterCount; ++i)
    {
        for (int k = 0; k < m_Emitters[i]->colorKeyCount; ++k)
        {
            allColorKeys.push_back(m_Emitters[i]->colorKeys[k]);
        }
    }

    if (!allColorKeys.empty())
    {
        size_t keyCount = (std::min)(allColorKeys.size(), static_cast<size_t>(MAX_COLOR_KEYS_TOTAL));

        D3D11_MAPPED_SUBRESOURCE colorMapped = {};
        hr = context->Map(m_ColorKeyBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &colorMapped);
        if (SUCCEEDED(hr))
        {
            memcpy(colorMapped.pData, allColorKeys.data(), sizeof(ColorKey) * keyCount);
            context->Unmap(m_ColorKeyBuffer.Get(), 0);
        }
    }
}

void GPUParticleSystem::DispatchEmit(ID3D11DeviceContext* context, uint32_t totalEmit)
{

    if (m_CurrentDeadCount == 0) return;

    totalEmit = (std::min)(totalEmit, m_CurrentDeadCount);
    if (totalEmit == 0) return;

    DeadListCB dlcb = {};
    dlcb.deadCount = m_CurrentDeadCount;
    dlcb.maxParticles = m_MaxParticles;

    m_EmitCS->WriteBuffer(context, 0, &m_CachedGlobalCB);
    m_EmitCS->WriteBuffer(context, 1, &dlcb);

    m_EmitCS->Bind(context);

    context->CSSetShaderResources(0, 1, m_EmitterSRV.GetAddressOf());

    ID3D11UnorderedAccessView* uavs[2] = { m_ParticleUAV.Get(), m_DeadList.GetUAV() };
    UINT initialCounts[2] = { (UINT)-1, m_CurrentDeadCount };
    context->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    context->Dispatch((totalEmit + 255) / 256, 1, 1);

    ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
    UINT zeros[2] = { 0, 0 };
    context->CSSetUnorderedAccessViews(0, 2, nullUAVs, zeros);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);
}
void GPUParticleSystem::DispatchUpdate(ID3D11DeviceContext* context)
{
    // b0: GlobalCB
    m_UpdateCS->WriteBuffer(context, 0, &m_CachedGlobalCB);

    // b1: DeadListCB（g_MaxParticlesが必要）
    DeadListCB dlcb = {};
    dlcb.deadCount = m_CurrentDeadCount;
    dlcb.maxParticles = m_MaxParticles;
    m_UpdateCS->WriteBuffer(context, 1, &dlcb);
    
    // 先にBind
    m_UpdateCS->Bind(context);

    // Bindの後にSRVとUAV
    context->CSSetShaderResources(0, 1, m_ColorKeySRV.GetAddressOf());

    ID3D11UnorderedAccessView* uavs[2] = { m_ParticleUAV.Get(), m_DeadList.GetUAV() };
    UINT initialCounts[2] = { (UINT)-1, (UINT)-1 };
    context->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    context->Dispatch((m_MaxParticles + 255) / 256, 1, 1);

    ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
    UINT zeros[2] = { 0, 0 };
    context->CSSetUnorderedAccessViews(0, 2, nullUAVs, zeros);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);
}
void GPUParticleSystem::UploadExternalEmitters(ID3D11DeviceContext* context,
    const std::vector<GPUEmitter>& emitters,
    const std::vector<ColorKey>& colorKeys)
{
    if (emitters.empty()) return;

    // Emitterアップロード
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(m_EmitterBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    size_t count = (std::min)(emitters.size(), static_cast<size_t>(MAX_EMITTERS));
    memcpy(mapped.pData, emitters.data(), sizeof(GPUEmitter) * count);
    context->Unmap(m_EmitterBuffer.Get(), 0);

    // ColorKeyアップロード
    if (!colorKeys.empty())
    {
        size_t keyCount = (std::min)(colorKeys.size(), static_cast<size_t>(MAX_COLOR_KEYS_TOTAL));
        D3D11_MAPPED_SUBRESOURCE colorMapped = {};
        hr = context->Map(m_ColorKeyBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &colorMapped);
        if (SUCCEEDED(hr))
        {
            memcpy(colorMapped.pData, colorKeys.data(), sizeof(ColorKey) * keyCount);
            context->Unmap(m_ColorKeyBuffer.Get(), 0);
        }
    }
}
void GPUParticleSystem::Render()
{
    if (!m_Camera) return;
    if (!m_RenderVS || !m_RenderVS->IsValid())
    {
        std::cout << "[Error] RenderVS invalid" << std::endl;
        return;
    }
    if (!m_RenderPS || !m_RenderPS->IsValid())
    {
        std::cout << "[Error] RenderPS invalid" << std::endl;
        return;
    }
    auto context = m_Context;

    ParticleRenderCB rcb = {};
    rcb.view = m_Camera->GetViewMatrix().Transpose();
    rcb.projection = m_Camera->GetProjectionMatrix().Transpose();
    rcb.cameraPosition = m_Camera->GetPosition();

    m_RenderVS->WriteBuffer(context, 0, &rcb);

    context->VSSetShaderResources(0, 1, m_ParticleSRV.GetAddressOf());

    if (m_Texture)
    {
        m_RenderPS->SetTexture(context, 0, m_Texture.get());
    }

    context->PSSetSamplers(0, 1, m_SamplerState.GetAddressOf());

    float blendFactor[4] = { 0, 0, 0, 0 };
    context->OMSetBlendState(m_BlendState.Get(), blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(m_DepthStencilState.Get(), 0);
    context->RSSetState(m_RasterizerState.Get());

    m_RenderVS->Bind(context);
    m_RenderPS->Bind(context);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    context->Draw(m_MaxParticles * 6, 0);

    context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(nullptr, 0);
    context->RSSetState(nullptr);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->VSSetShaderResources(0, 1, &nullSRV);
}

void GPUParticleSystem::ResetSystem ()
{
    std::vector<GPUParticle> emptyParticles(m_MaxParticles, GPUParticle{});
    m_Context->UpdateSubresource(m_ParticleBuffer.Get(), 0, nullptr, emptyParticles.data(), 0, 0);

    DeadListCB dlcb = {};
    dlcb.deadCount = 0;
    dlcb.maxParticles = m_MaxParticles;

    m_InitDeadListCS->WriteBuffer(m_Context, 0, &dlcb);
    m_InitDeadListCS->Bind(m_Context);

    m_DeadList.BindCSUAV(m_Context, 0, 0);
    m_Context->Dispatch((m_MaxParticles + 255) / 256, 1, 1);

    m_DeadList.UnbindCSUAV(m_Context, 0);
    m_InitDeadListCS->Unbind(m_Context);

    m_CurrentDeadCount = m_DeadList.ReadDeadCount(m_Context);
}