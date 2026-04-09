#pragma once
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

template <typename T>
class VertexBuffer
{
public:
    // 作成（静的 or 动的を選択可能）
    bool Create(
        ID3D11Device* device,
        const std::vector<T>& vertices,
        bool dynamic = true)
    {
        if (!device || vertices.empty())
            return false;

        m_VertexCount = static_cast<UINT>(vertices.size());
        m_Dynamic = dynamic;

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(T) * m_VertexCount;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        if (dynamic)
        {
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
        else
        {
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.CPUAccessFlags = 0;
        }

        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = vertices.data();

        return SUCCEEDED(device->CreateBuffer(&bd, &sd, &m_Buffer));
    }

    // 空のバッファを作成（後で書き換え前提）
    bool CreateEmpty(
        ID3D11Device* device,
        UINT vertexCount)
    {
        if (!device || vertexCount == 0)
            return false;

        m_VertexCount = vertexCount;
        m_Dynamic = true;

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(T) * vertexCount;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        return SUCCEEDED(device->CreateBuffer(&bd, nullptr, &m_Buffer));
    }

    // GPUにセット
    void SetGPU(ID3D11DeviceContext* context) const
    {
        if (!context || !m_Buffer)
            return;

        UINT stride = sizeof(T);
        UINT offset = 0;

        context->IASetVertexBuffers(
            0,
            1,
            m_Buffer.GetAddressOf(),
            &stride,
            &offset);
    }

    // 書き換え（Dynamic のみ）
    bool Modify(
        ID3D11DeviceContext* context,
        const std::vector<T>& vertices)
    {
        if (!context || !m_Buffer || !m_Dynamic)
            return false;

        if (vertices.empty())
            return false;

        // サイズオーバーなら再作成
        if (vertices.size() > m_VertexCount)
        {
            return ReCreate(context, vertices);
        }

        D3D11_MAPPED_SUBRESOURCE msr;
        HRESULT hr = context->Map(
            m_Buffer.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &msr);

        if (FAILED(hr))
            return false;

        memcpy(msr.pData, vertices.data(), vertices.size() * sizeof(T));
        context->Unmap(m_Buffer.Get(), 0);

        return true;
    }

    // 強制的に再作成
    bool ReCreate(
        ID3D11DeviceContext* context,
        const std::vector<T>& vertices)
    {
        if (!context)
            return false;

        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);

        return Create(device.Get(), vertices, m_Dynamic);
    }

    UINT GetVertexCount() const { return m_VertexCount; }

    bool IsDynamic() const { return m_Dynamic; }

    bool IsValid() const { return m_Buffer != nullptr; }

private:
    ComPtr<ID3D11Buffer> m_Buffer;
    UINT m_VertexCount = 0;
    bool m_Dynamic = true;
};
