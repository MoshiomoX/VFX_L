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

        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = indices.data();

        return SUCCEEDED(device->CreateBuffer(&bd, &sd, &m_Buffer));
    }

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
    UINT m_IndexCount = 0;
};
