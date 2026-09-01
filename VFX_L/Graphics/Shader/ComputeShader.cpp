#include "Graphics/Shader/ComputeShader.h"
#include <iostream>

ComputeShader::ComputeShader()
    : Shader(Shader::Compute)
{}

void ComputeShader::Bind(ID3D11DeviceContext* context)
{
    if (!context) return;

    context->CSSetShader(m_CS.Get(), nullptr, 0);

    for (UINT i = 0; i < static_cast<UINT>(m_Buffers.size()); ++i)
    {
        if (m_Buffers[i])
            context->CSSetConstantBuffers(i, 1, m_Buffers[i].GetAddressOf());
    }
}

void ComputeShader::Unbind(ID3D11DeviceContext* context)
{
    if (!context) return;
    context->CSSetShader(nullptr, nullptr, 0);
}

HRESULT ComputeShader::MakeShader(ID3D11Device* device, void* pData, UINT size)
{
    HRESULT hr = device->CreateComputeShader(pData, size, nullptr, &m_CS);
    if (FAILED(hr))
    {
        std::cout << "[Error] CreateComputeShader failed" << std::endl;
    }
    return hr;
}