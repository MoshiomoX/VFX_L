#pragma once
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// T = 任意结构体（MVP矩阵、光照参数等）
template<typename T>
class ConstantBuffer
{
public:
    // 创建Buffer（初始化时调用一次）
    bool Create(ID3D11Device* device)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(T);                    // Buffer大小 = 结构体大小
        bd.Usage = D3D11_USAGE_DYNAMIC;              // CPU可写
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;   // 用途：ConstantBuffer
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;  // CPU写入权限

        return SUCCEEDED(device->CreateBuffer(&bd, nullptr, &m_Buffer));
    }

    // 更新数据（每帧或每次Draw前调用）
    void Update(ID3D11DeviceContext* context, const T& data)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        // 锁定Buffer，获取可写指针
        context->Map(m_Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        // 复制数据
        memcpy(mapped.pData, &data, sizeof(T));
        // 解锁
        context->Unmap(m_Buffer.Get(), 0);
    }

    // 绑定到Vertex Shader
    void BindVS(ID3D11DeviceContext* context, UINT slot)
    {

        context->VSSetConstantBuffers(slot, 1, m_Buffer.GetAddressOf());
    }

    // 绑定到Pixel Shader
    void BindPS(ID3D11DeviceContext* context, UINT slot)
    {
        context->PSSetConstantBuffers(slot, 1, m_Buffer.GetAddressOf());
    }

    void BindCS(ID3D11DeviceContext* context, UINT slot)
    {
        context->CSSetConstantBuffers(slot, 1, m_Buffer.GetAddressOf());
    }

private:
    ComPtr<ID3D11Buffer> m_Buffer;
};