#include "GPUParticleSystem.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "d3dcompiler.lib")

// hlslパス → csoパス
static std::string PtoCso(const std::wstring& hlslPath)
{
    std::string s(hlslPath.begin(), hlslPath.end());
    size_t dot = s.find_last_of('.');
    if (dot != std::string::npos)
        s = s.substr(0, dot);
    return s + ".cso";   // "Shader/Particle/GPUParticleVS.cso"
}

// VS/PS/CS 共通：Debug=hlslコンパイル / Release=csoロード
template <class T>
static HRESULT LoadShaderAuto(T* shader, ID3D11Device* device, const std::wstring& hlslPath)
{
#ifdef _DEBUG
    return shader->Compile(device, hlslPath);
#else
    return shader->Load(device, PtoCso(hlslPath).c_str());
#endif
}
// ============================================
// 初期化
// ============================================
bool GPUParticleSystem::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t maxParticles)
{
    m_Device = device;
    m_Context = context;
    m_MaxParticles = maxParticles;

    if (!CreateParticleBuffer(device))       return false;
    if (!CreateEmitterBuffer(device))        return false;
    if (!m_DeadList.Initialize(device, maxParticles)) return false;
    if (!LoadShaders(device))                return false;
    if (!CreateRenderStates(device))         return false;
    if (!CreateColorKeyBuffer(device))       return false;
    if (!CreateDrawIndirectBuffer(device))   return false;
    if (!CreateAliveListBuffer(device, maxParticles)) return false;

    // DeadList を全インデックスで初期化
    DeadListCB dlcb = {};
    dlcb.deadCount = 0;
    dlcb.maxParticles = maxParticles;

    m_InitDeadListCS->WriteBuffer(context, 0, &dlcb);
    m_InitDeadListCS->Bind(context);
    m_DeadList.BindCSUAV(context, 0, 0);

    context->Dispatch((maxParticles + 255) / 256, 1, 1);

    m_DeadList.UnbindCSUAV(context, 0);
    m_InitDeadListCS->Unbind(context);

    m_CurrentDeadCount = m_DeadList.ReadDeadCount(context);

    return true;
}

// ============================================
// バッファ作成
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

// ============================================
// DrawIndirect バッファ作成
// ============================================
bool GPUParticleSystem::CreateDrawIndirectBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(uint32_t) * 4;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;

    // InstanceCount 累加方式：VertexCount=6(固定), InstanceCount=0(CSが累加)
    uint32_t initArgs[4] = { 6, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = initArgs;

    HRESULT hr = device->CreateBuffer(&desc, &initData, &m_DrawIndirectBuffer);
    if (FAILED(hr))
    {
        std::cout << "[Error] DrawIndirectBuffer creation failed" << std::endl;
        return false;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_UINT;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 4;
    uavDesc.Buffer.Flags = 0;

    hr = device->CreateUnorderedAccessView(m_DrawIndirectBuffer.Get(), &uavDesc, &m_DrawIndirectUAV);
    if (FAILED(hr))
    {
        std::cout << "[Error] DrawIndirectUAV creation failed" << std::endl;
        return false;
    }

    std::cout << "[OK] DrawIndirectBuffer created" << std::endl;
    return true;
}

// ============================================
// AliveList バッファ作成
// ============================================
bool GPUParticleSystem::CreateAliveListBuffer(ID3D11Device* device, uint32_t maxParticles)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(uint32_t) * maxParticles;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(uint32_t);

    if (FAILED(device->CreateBuffer(&desc, nullptr, &m_AliveListBuffer)))
        return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = maxParticles;

    if (FAILED(device->CreateUnorderedAccessView(m_AliveListBuffer.Get(), &uavDesc, &m_AliveListUAV)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = maxParticles;

    if (FAILED(device->CreateShaderResourceView(m_AliveListBuffer.Get(), &srvDesc, &m_AliveListSRV)))
        return false;

    std::cout << "[OK] AliveList Buffer created" << std::endl;
    return true;
}

// ============================================
// シェーダー読み込み
// ============================================
bool GPUParticleSystem::LoadShaders(ID3D11Device* device)
{
    m_InitDeadListCS = std::make_shared<ComputeShader>();
    HRESULT hr = LoadShaderAuto(m_InitDeadListCS.get(), device, L"Shader/Particle/InitDeadListCS.hlsl");
    std::cout << "[LoadShaders] InitDeadListCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_EmitCS = std::make_shared<ComputeShader>();
    hr = LoadShaderAuto(m_EmitCS.get(), device, L"Shader/Particle/ParticleEmitCS.hlsl");
    std::cout << "[LoadShaders] EmitCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_UpdateCS = std::make_shared<ComputeShader>();
    hr = LoadShaderAuto(m_UpdateCS.get(), device, L"Shader/Particle/ParticleUpdateCS.hlsl");
    std::cout << "[LoadShaders] UpdateCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_RenderVS = std::make_shared<VertexShader>();
    hr = LoadShaderAuto(m_RenderVS.get(), device, L"Shader/Particle/GPUParticleVS.hlsl");
    std::cout << "[LoadShaders] RenderVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_RenderPS = std::make_shared<PixelShader>();
    hr = LoadShaderAuto(m_RenderPS.get(), device, L"Shader/Particle/GPUParticlePS.hlsl");
    std::cout << "[LoadShaders] RenderPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

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
// 毎フレーム更新
// ============================================
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

    uint32_t totalEmit = 0;
    for (auto& e : emitters)
    {
        if (e.isActive > 0.5f)
            totalEmit += e.emitCount;
    }

    UploadExternalEmitters(context, emitters, colorKeys);
    DispatchEmit(context, totalEmit);
    DispatchUpdate(context);
}

void GPUParticleSystem::UploadExternalEmitters(ID3D11DeviceContext* context,
    const std::vector<GPUEmitter>& emitters,
    const std::vector<ColorKey>& colorKeys)
{
    if (emitters.empty()) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(m_EmitterBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    size_t count = (std::min)(emitters.size(), static_cast<size_t>(MAX_EMITTERS));
    memcpy(mapped.pData, emitters.data(), sizeof(GPUEmitter) * count);
    context->Unmap(m_EmitterBuffer.Get(), 0);

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

// ============================================
// Emit ディスパッチ（一括バインド版）
// ============================================
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

    // SRV は即時バインド
    m_EmitCS->SetSRV(context, "emitters", m_EmitterSRV.Get());

    // UAV はキューに溜めて一括バインド
    m_EmitCS->SetUAV(context, "particles", m_ParticleUAV.Get());
    m_EmitCS->SetUAV(context, "deadList", m_DeadList.GetUAV(), m_CurrentDeadCount);
    m_EmitCS->BindUAVs(context);

    context->Dispatch((totalEmit + 255) / 256, 1, 1);

    m_EmitCS->UnbindSRVs(context);
    m_EmitCS->UnbindUAVs(context);
}

// ============================================
// Update ディスパッチ（DrawIndirect + AliveList + 一括バインド）
// ============================================
void GPUParticleSystem::DispatchUpdate(ID3D11DeviceContext* context)
{
    DeadListCB dlcb = {};
    dlcb.deadCount = m_CurrentDeadCount;
    dlcb.maxParticles = m_MaxParticles;

    m_UpdateCS->WriteBuffer(context, 0, &m_CachedGlobalCB);
    m_UpdateCS->WriteBuffer(context, 1, &dlcb);

    // DrawIndirectArgs リセット（InstanceCount 累加方式）
    UINT clearValues[4] = { 6, 0, 0, 0 };
    context->ClearUnorderedAccessViewUint(m_DrawIndirectUAV.Get(), clearValues);

    m_UpdateCS->Bind(context);

    // SRV は即時バインド
    m_UpdateCS->SetSRV(context, "colorKeys", m_ColorKeySRV.Get());

    // UAV はキューに溜めて一括バインド
    m_UpdateCS->SetUAV(context, "particles", m_ParticleUAV.Get());
    m_UpdateCS->SetUAV(context, "deadList", m_DeadList.GetUAV());
    m_UpdateCS->SetUAV(context, "g_DrawArgs", m_DrawIndirectUAV.Get());
    m_UpdateCS->SetUAV(context, "aliveList", m_AliveListUAV.Get());
    m_UpdateCS->BindUAVs(context);

    context->Dispatch((m_MaxParticles + 255) / 256, 1, 1);

    m_UpdateCS->UnbindSRVs(context);
    m_UpdateCS->UnbindUAVs(context);
}

// ============================================
// レンダリング（DrawInstancedIndirect + AliveList）
// ============================================
void GPUParticleSystem::Render()
{
    if (!m_Camera) return;
    if (!m_RenderVS || !m_RenderVS->IsValid()) return;
    if (!m_RenderPS || !m_RenderPS->IsValid()) return;

    auto context = m_Context;

    // ========== GPU タイミング計測 ==========
    static ComPtr<ID3D11Query> queryBegin, queryEnd, queryDisjoint;
    static bool queryCreated = false;
    static int frameCount = 0;

    if (!queryCreated)
    {
        D3D11_QUERY_DESC qd = {};
        qd.Query = D3D11_QUERY_TIMESTAMP;
        m_Device->CreateQuery(&qd, &queryBegin);
        m_Device->CreateQuery(&qd, &queryEnd);

        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        m_Device->CreateQuery(&qd, &queryDisjoint);
        queryCreated = true;
    }

    context->Begin(queryDisjoint.Get());
    context->End(queryBegin.Get());
    // ========================================

    // --- 既存の描画処理 ---
    ParticleRenderCB rcb = {};
    rcb.view = m_Camera->GetViewMatrix().Transpose();
    rcb.projection = m_Camera->GetProjectionMatrix().Transpose();
    rcb.cameraPosition = m_Camera->GetPosition();

    m_RenderVS->WriteBuffer(context, 0, &rcb);
    m_RenderVS->SetSRV(context, "particles", m_ParticleSRV.Get());
    m_RenderVS->SetSRV(context, "aliveList", m_AliveListSRV.Get());

    if (m_Texture)
        m_RenderPS->SetTexture(context, 0, m_Texture.get());

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

    context->DrawInstancedIndirect(m_DrawIndirectBuffer.Get(), 0);

    context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(nullptr, 0);
    context->RSSetState(nullptr);

    m_RenderVS->UnbindSRVs(context);

    // ========== GPU タイミング結果取得 ==========
    context->End(queryEnd.Get());
    context->End(queryDisjoint.Get());

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
    while (context->GetData(queryDisjoint.Get(), &disjointData,
        sizeof(disjointData), 0) == S_FALSE) {
    }

    if (!disjointData.Disjoint)
    {
        UINT64 tsBegin, tsEnd;
        context->GetData(queryBegin.Get(), &tsBegin, sizeof(tsBegin), 0);
        context->GetData(queryEnd.Get(), &tsEnd, sizeof(tsEnd), 0);

        double gpuMs = (tsEnd - tsBegin) / (double)disjointData.Frequency * 1000.0;

     }
    // ==========================================
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

    m_InitDeadListCS->WriteBuffer(m_Context, 0, &dlcb);
    m_InitDeadListCS->Bind(m_Context);

    m_DeadList.BindCSUAV(m_Context, 0, 0);
    m_Context->Dispatch((m_MaxParticles + 255) / 256, 1, 1);

    m_DeadList.UnbindCSUAV(m_Context, 0);
    m_InitDeadListCS->Unbind(m_Context);

    m_CurrentDeadCount = m_DeadList.ReadDeadCount(m_Context);

    // DrawIndirectArgs もリセット
    UINT clearValues[4] = { 6, 0, 0, 0 };
    m_Context->ClearUnorderedAccessViewUint(m_DrawIndirectUAV.Get(), clearValues);
}