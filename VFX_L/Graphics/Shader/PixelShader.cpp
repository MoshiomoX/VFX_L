#include "Graphics/Shader/PixelShader.h"
#include <iostream>

PixelShader::PixelShader()
    : Shader(Shader::Pixel)
{}

void PixelShader::Bind(ID3D11DeviceContext* context)
{
    if (!context) return;

    context->PSSetShader(m_PS.Get(), nullptr, 0);

    for (UINT i = 0; i < static_cast<UINT>(m_Buffers.size()); ++i)
    {
        if (m_Buffers[i])
            context->PSSetConstantBuffers(i, 1, m_Buffers[i].GetAddressOf());
    }
}

void PixelShader::Unbind(ID3D11DeviceContext* context)
{
    if (!context) return;
    context->PSSetShader(nullptr, nullptr, 0);
}

HRESULT PixelShader::MakeShader(ID3D11Device* device, void* pData, UINT size)
{
    HRESULT hr = device->CreatePixelShader(pData, size, nullptr, &m_PS);
    if (FAILED(hr))
    {
        std::cout << "[Error] CreatePixelShader failed" << std::endl;
    }
    return hr;
}