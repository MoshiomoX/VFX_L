#pragma once
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

template <typename T = unsigned int>
class IndexBuffer
{
public:
    bool Create(ID3D11Device* device, const std::vector<T>& indices)
    {
        if (!device || indices.empty())
            return false;

        m_IndexCount = static_cast<UINT>(indices.size());

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(T) * m_IndexCount;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;

        // 粒子の Mesh 発射（三角形を選んで面上の点を取る）用に CS から raw view で読めるようにする
        const bool rawView = (bd.ByteWidth % 4 == 0);
        if (rawView)
        {
            bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            bd.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        }

        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = indices.data();

        m_RawSRV.Reset();
        if (FAILED(device->CreateBuffer(&bd, &sd, &m_Buffer)))
            return false;

        if (rawView)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
            sv.Format = DXGI_FORMAT_R32_TYPELESS;
            sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            sv.BufferEx.FirstElement = 0;
            sv.BufferEx.NumElements = bd.ByteWidth / 4;
            sv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            device->CreateShaderResourceView(m_Buffer.Get(), &sv, &m_RawSRV);
        }
        return true;
    }

    // 粒子の Mesh 発射源用 raw view（ByteAddressBuffer）。無ければ nullptr
    ID3D11ShaderResourceView* GetRawSRV() const { return m_RawSRV.Get(); }
    static constexpr UINT GetIndexBytes() { return (UINT)sizeof(T); }

    void SetGPU(ID3D11DeviceContext* context)
    {
        if (!context || !m_Buffer)
            return;

        DXGI_FORMAT format = (sizeof(T) == 2)
            ? DXGI_FORMAT_R16_UINT
            : DXGI_FORMAT_R32_UINT;

        context->IASetIndexBuffer(m_Buffer.Get(), format, 0);
    }

    UINT GetIndexCount() const { return m_IndexCount; }

private:
    ComPtr<ID3D11Buffer> m_Buffer;
    ComPtr<ID3D11ShaderResourceView> m_RawSRV;
    UINT m_IndexCount = 0;
};
