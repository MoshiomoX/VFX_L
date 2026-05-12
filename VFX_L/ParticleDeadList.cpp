#include "ParticleDeadList.h"

bool ParticleDeadList::Initialize(ID3D11Device* device, uint32_t maxParticles)
{
    m_MaxParticles = maxParticles;

    // ----------------------------------------
    // 1. Buffer本体 (AppendStructuredBuffer<uint>)
    // ----------------------------------------
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(uint32_t) * maxParticles;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(uint32_t);

    HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, &m_Buffer);
    if (FAILED(hr)) return false;

    // ----------------------------------------
    // 2. UAV (APPEND対応)
    // ----------------------------------------
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = maxParticles;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND; // ← Append/Consume有効

    hr = device->CreateUnorderedAccessView(m_Buffer.Get(), &uavDesc, &m_UAV);
    if (FAILED(hr)) return false;

    // ----------------------------------------
    // 3. SRV (読み取り専用アクセス用、必要に応じて)
    // ----------------------------------------
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = maxParticles;

    hr = device->CreateShaderResourceView(m_Buffer.Get(), &srvDesc, &m_SRV);
    if (FAILED(hr)) return false;

    // ----------------------------------------
    // 4. カウント読み戻し用ステージングバッファ
    //    UAVの内部カウンタをCPUに読むために必要
    // ----------------------------------------
    D3D11_BUFFER_DESC stagingDesc = {};
    stagingDesc.ByteWidth = sizeof(uint32_t);
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = device->CreateBuffer(&stagingDesc, nullptr, &m_CountStaging);
    if (FAILED(hr)) return false;

    return true;
}

uint32_t ParticleDeadList::ReadDeadCount(ID3D11DeviceContext* context)
{
    // UAVの内部カウンタをステージングバッファにコピー
    context->CopyStructureCount(m_CountStaging.Get(), 0, m_UAV.Get());

    // CPU側に読み戻す
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(m_CountStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return 0;

    uint32_t count = *static_cast<uint32_t*>(mapped.pData);
    context->Unmap(m_CountStaging.Get(), 0);

    return count;
}

void ParticleDeadList::BindCSUAV(ID3D11DeviceContext* context, uint32_t slot, uint32_t initialCount)
{
    context->CSSetUnorderedAccessViews(slot, 1, m_UAV.GetAddressOf(), &initialCount);
}

void ParticleDeadList::UnbindCSUAV(ID3D11DeviceContext* context, uint32_t slot)
{
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    UINT zero = 0;
    context->CSSetUnorderedAccessViews(slot, 1, &nullUAV, &zero);
}