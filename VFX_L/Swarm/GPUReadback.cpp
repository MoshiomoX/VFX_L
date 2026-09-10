// ============================================================
// GPUReadback.cpp
// ============================================================
#include "Swarm/GPUReadback.h"
#include <iostream>

using Microsoft::WRL::ComPtr;

bool GPUReadback::Initialize(ID3D11Device* device)
{
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(SwarmCounters);
    bd.Usage = D3D11_USAGE_STAGING;
    bd.BindFlags = 0;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    bd.MiscFlags = 0;

    for (int i = 0; i < kFrames; ++i)
    {
        if (FAILED(device->CreateBuffer(&bd, nullptr, &m_Staging[i])))
        {
            std::cout << "[Error] GPUReadback: staging buffer " << i << " failed" << std::endl;
            return false;
        }
        m_Filled[i] = false;
    }

    m_WriteIndex = 0;
    m_Latest = {};

    std::cout << "[OK] GPUReadback initialized" << std::endl;
    return true;
}

void GPUReadback::Shutdown()
{
    for (auto& s : m_Staging) s.Reset();
}

// ============================================================
// GPU → staging へ写す（CPU は待たない）
// ============================================================
void GPUReadback::RequestCopy(ID3D11DeviceContext* ctx, ID3D11Buffer* src)
{
    if (!src) return;

    ctx->CopyResource(m_Staging[m_WriteIndex].Get(), src);
    m_Filled[m_WriteIndex] = true;

    m_WriteIndex = (m_WriteIndex + 1) % kFrames;
}

// ============================================================
// 一番古い staging を読む（書き終わっていなければ諦める）
//
// m_WriteIndex は「次に書く先」なので、そこが最古。
// 3 枚ある以上、そこへの Copy は 2 フレーム前に発行済みで、
// 普通は完了している。それでも DO_NOT_WAIT を外さない:
//   コマ落ちや GPU 側の詰まりで完了していない可能性は残るし、
//   その時に待ってしまうと管線が切れる。
// ============================================================
bool GPUReadback::TryRead(ID3D11DeviceContext* ctx, SwarmCounters& out)
{
    const int readIndex = m_WriteIndex;   // 次に書く = 一番古い
    if (!m_Filled[readIndex]) return false;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = ctx->Map(m_Staging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);

    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return false;   // まだ。次フレームで
    if (FAILED(hr)) return false;

    memcpy(&m_Latest, mapped.pData, sizeof(SwarmCounters));
    ctx->Unmap(m_Staging[readIndex].Get(), 0);

    out = m_Latest;
    return true;
}