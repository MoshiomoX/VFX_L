#include "GPUParticleSystem.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "d3dcompiler.lib")

// ============================================
// 初期化
// ============================================
bool GPUParticleSystem::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t maxParticles)
{
    m_Device = device;
    m_Context = context;
    m_MaxParticles = maxParticles;

    if (!CreateParticleBuffer(device))   return false;
    if (!CreateEmitterBuffer(device))    return false;
    if (!m_DeadList.Initialize(device, maxParticles)) return false;
    if (!LoadComputeShaders(device))     return false;
    if (!CreateConstantBuffers(device))  return false;
    if (!CreateRenderStates(device))     return false;
    if (!CreateColorKeyBuffer(device))   return false;

    // Dead Listを全インデックスで初期化
    DeadListCB dlcb = {};
    dlcb.deadCount = 0;
    dlcb.maxParticles = maxParticles;
    m_DeadListCB.Update(context, dlcb);
    m_DeadListCB.BindCS(context, 0);

    m_DeadList.BindCSUAV(context, 0, 0);
    m_InitDeadListCS.BindCS(context);
    context->Dispatch((maxParticles + 255) / 256, 1, 1);

    m_DeadList.UnbindCSUAV(context, 0);
    m_InitDeadListCS.UnbindCS(context);

    m_CurrentDeadCount = m_DeadList.ReadDeadCount(context);

    return true;
}

// ============================================
// 粒子バッファ作成
// ============================================
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

// ============================================
// ColorKeyバッファ作成
// ============================================
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

// ============================================
// 発射器バッファ作成
// ============================================
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

// ============================================
// CSロード
// ============================================
bool GPUParticleSystem::LoadComputeShaders(ID3D11Device* device)
{
    if (!m_InitDeadListCS.LoadCS(device, L"Shader/Particle/InitDeadListCS.hlsl"))
        return false;

    if (!m_EmitCS.LoadCS(device, L"Shader/Particle/ParticleEmitCS.hlsl"))
        return false;

    if (!m_UpdateCS.LoadCS(device, L"Shader/Particle/ParticleUpdateCS.hlsl"))
        return false;

    if (!m_RenderShader.LoadParticle(device, L"Shader/Particle/GPUParticleVS.hlsl", L"Shader/Particle/GPUParticlePS.hlsl"))
        return false;

    return true;
}

// ============================================
// 定数バッファ作成
// ============================================
bool GPUParticleSystem::CreateConstantBuffers(ID3D11Device* device)
{
    if (!m_GlobalCB.Create(device))   return false;
    if (!m_DeadListCB.Create(device)) return false;
    if (!m_RenderCB.Create(device))   return false;
    return true;
}

// ============================================
// レンダーステート作成
// ============================================
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

// ============================================
// 毎フレーム更新（外部からデータを受け取る）
// ============================================
void GPUParticleSystem::Update(float deltaTime, float totalTime,
    const std::vector<GPUEmitter>& emitters,
    const std::vector<ColorKey>& colorKeys)
{
    auto context = m_Context;

    // Dead Listのカウントを読み取り
    m_CurrentDeadCount = m_DeadList.ReadDeadCount(context);

    // GlobalCB更新
    GlobalCB gcb = {};
    gcb.deltaTime = deltaTime;
    gcb.totalTime = totalTime;
    gcb.baseSeed = static_cast<uint32_t>(totalTime * 1000.0f);
    gcb.emitterCount = static_cast<int>(emitters.size());
    m_GlobalCB.Update(context, gcb);

    // DeadListCB更新
    DeadListCB dlcb = {};
    dlcb.deadCount = m_CurrentDeadCount;
    dlcb.maxParticles = m_MaxParticles;
    m_DeadListCB.Update(context, dlcb);

    // 外部データをアップロード
    UploadEmitters(context, emitters);
    UploadColorKeys(context, colorKeys);

    // CS dispatch
    DispatchEmit(context, emitters);
    DispatchUpdate(context);
}

// ============================================
// 発射器データのアップロード
// ============================================
void GPUParticleSystem::UploadEmitters(ID3D11DeviceContext* context,
    const std::vector<GPUEmitter>& emitters)
{
    if (emitters.empty()) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(m_EmitterBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    memcpy(mapped.pData, emitters.data(), sizeof(GPUEmitter) * emitters.size());
    context->Unmap(m_EmitterBuffer.Get(), 0);
}

// ============================================
// ColorKeyデータのアップロード
// ============================================
void GPUParticleSystem::UploadColorKeys(ID3D11DeviceContext* context,
    const std::vector<ColorKey>& colorKeys)
{
    if (colorKeys.empty()) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(m_ColorKeyBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    memcpy(mapped.pData, colorKeys.data(), sizeof(ColorKey) * colorKeys.size());
    context->Unmap(m_ColorKeyBuffer.Get(), 0);
}

// ============================================
// Emit dispatch
// ============================================
void GPUParticleSystem::DispatchEmit(ID3D11DeviceContext* context,
    const std::vector<GPUEmitter>& emitters)
{
    if (m_CurrentDeadCount == 0) return;
    if (emitters.empty()) return;

    // CBバインド
    m_GlobalCB.BindCS(context, 0);
    m_DeadListCB.BindCS(context, 1);

    // SRV: 発射器データ
    context->CSSetShaderResources(0, 1, m_EmitterSRV.GetAddressOf());

    // UAV: 粒子バッファ(u0) + Dead List(u1)
    ID3D11UnorderedAccessView* uavs[2] = { m_ParticleUAV.Get(), m_DeadList.GetUAV() };
    UINT initialCounts[2] = { (UINT)-1, m_CurrentDeadCount };
    context->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    // dispatch
    m_EmitCS.BindCS(context);

    // 全発射器の合計発射数を計算
    uint32_t totalEmit = 0;
    for (auto& e : emitters)
    {
        if (e.isActive > 0.5f)
            totalEmit += e.emitCount;
    }
    totalEmit = min(totalEmit, m_CurrentDeadCount);

    if (totalEmit > 0)
    {
        context->Dispatch((totalEmit + 255) / 256, 1, 1);
    }

    // 解除
    ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
    UINT zeros[2] = { 0, 0 };
    context->CSSetUnorderedAccessViews(0, 2, nullUAVs, zeros);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);
}

// ============================================
// Update dispatch
// ============================================
void GPUParticleSystem::DispatchUpdate(ID3D11DeviceContext* context)
{
    m_GlobalCB.BindCS(context, 0);

    // ColorKey SRVをt0にバインド
    context->CSSetShaderResources(0, 1, m_ColorKeySRV.GetAddressOf());

    ID3D11UnorderedAccessView* uavs[2] = { m_ParticleUAV.Get(), m_DeadList.GetUAV() };
    UINT initialCounts[2] = { (UINT)-1, (UINT)-1 };
    context->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    m_UpdateCS.BindCS(context);
    context->Dispatch((m_MaxParticles + 255) / 256, 1, 1);

    // 解除
    ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
    UINT zeros[2] = { 0, 0 };
    context->CSSetUnorderedAccessViews(0, 2, nullUAVs, zeros);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);
}

// ============================================
// レンダリング
// ============================================
void GPUParticleSystem::Render()
{
    if (!m_Camera) return;

    auto context = m_Context;

    // RenderCB更新
    ParticleRenderCB rcb = {};
    rcb.view = m_Camera->GetViewMatrix().Transpose();
    rcb.projection = m_Camera->GetProjectionMatrix().Transpose();
    rcb.cameraPosition = m_Camera->GetPosition();
    m_RenderCB.Update(context, rcb);
    m_RenderCB.BindVS(context, 0);

    // 粒子バッファをVS SRVとしてバインド
    context->VSSetShaderResources(0, 1, m_ParticleSRV.GetAddressOf());

    // テクスチャ
    if (m_Texture)
    {
        m_Texture->Bind(context, 0);
    }

    context->PSSetSamplers(0, 1, m_SamplerState.GetAddressOf());

    // レンダーステート設定
    float blendFactor[4] = { 0, 0, 0, 0 };
    context->OMSetBlendState(m_BlendState.Get(), blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(m_DepthStencilState.Get(), 0);
    context->RSSetState(m_RasterizerState.Get());

    // シェーダーバインド
    m_RenderShader.Bind(context);

    // 頂点バッファなし、SV_VertexIDでビルボード生成
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    // 1粒子 = 6頂点（quad）、全粒子分draw
    context->Draw(m_MaxParticles * 6, 0);

    // レンダーステート復元
    context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(nullptr, 0);
    context->RSSetState(nullptr);

    // SRV解除
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->VSSetShaderResources(0, 1, &nullSRV);
}

// ============================================
// システムリセット
// ============================================
void GPUParticleSystem::ResetSystem()
{
    std::vector<GPUParticle> emptyParticles(m_MaxParticles, GPUParticle{});
    m_Context->UpdateSubresource(m_ParticleBuffer.Get(), 0, nullptr, emptyParticles.data(), 0, 0);

    DeadListCB dlcb = {};
    dlcb.deadCount = 0;
    dlcb.maxParticles = m_MaxParticles;
    m_DeadListCB.Update(m_Context, dlcb);
    m_DeadListCB.BindCS(m_Context, 0);

    m_DeadList.BindCSUAV(m_Context, 0, 0);
    m_InitDeadListCS.BindCS(m_Context);
    m_Context->Dispatch((m_MaxParticles + 255) / 256, 1, 1);

    m_DeadList.UnbindCSUAV(m_Context, 0);
    m_InitDeadListCS.UnbindCS(m_Context);

    m_CurrentDeadCount = m_DeadList.ReadDeadCount(m_Context);
}