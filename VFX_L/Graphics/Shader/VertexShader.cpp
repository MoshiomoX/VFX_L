#include "Graphics/Shader/VertexShader.h"
#include <iostream>

VertexShader::VertexShader()
    : Shader(Shader::Vertex)
{}

// ============================================
// パイプラインにバインド
// ============================================
void VertexShader::Bind(ID3D11DeviceContext* context)
{
    if (!context) return;

    context->VSSetShader(m_VS.Get(), nullptr, 0);
    context->IASetInputLayout(m_InputLayout.Get());

    for (UINT i = 0; i < static_cast<UINT>(m_Buffers.size()); ++i)
    {
        if (m_Buffers[i])
            context->VSSetConstantBuffers(i, 1, m_Buffers[i].GetAddressOf());
    }
}

// ============================================
// バインド解除
// ============================================
void VertexShader::Unbind(ID3D11DeviceContext* context)
{
    if (!context) return;
    context->VSSetShader(nullptr, nullptr, 0);
}

// ============================================
// VSオブジェクト作成 + InputLayout自動生成
// ============================================
HRESULT VertexShader::MakeShader(ID3D11Device* device, void* pData, UINT size)
{
    HRESULT hr;

    // --- 頂点シェーダーオブジェクト作成 ---
    hr = device->CreateVertexShader(pData, size, nullptr, &m_VS);
    if (FAILED(hr))
    {
        std::cout << "[Error] CreateVertexShader failed" << std::endl;
        return hr;
    }

    // --- Reflection → InputLayout自動生成 ---
    ComPtr<ID3D11ShaderReflection> pReflection;
    hr = D3DReflect(pData, size, IID_PPV_ARGS(&pReflection));
    if (FAILED(hr))
    {
        std::cout << "[Error] D3DReflect failed in VertexShader" << std::endl;
        return hr;
    }

    D3D11_SHADER_DESC shaderDesc;
    pReflection->GetDesc(&shaderDesc);

    // フォーマットテーブル: [型][成分数-1]
    DXGI_FORMAT formats[][4] =
    {
        // UINT
        {
            DXGI_FORMAT_R32_UINT,
            DXGI_FORMAT_R32G32_UINT,
            DXGI_FORMAT_R32G32B32_UINT,
            DXGI_FORMAT_R32G32B32A32_UINT,
        },
        // SINT
        {
            DXGI_FORMAT_R32_SINT,
            DXGI_FORMAT_R32G32_SINT,
            DXGI_FORMAT_R32G32B32_SINT,
            DXGI_FORMAT_R32G32B32A32_SINT,
        },
        // FLOAT
        {
            DXGI_FORMAT_R32_FLOAT,
            DXGI_FORMAT_R32G32_FLOAT,
            DXGI_FORMAT_R32G32B32_FLOAT,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
        },
    };

    if (shaderDesc.InputParameters == 0)
    {
        return S_OK;
    }
    // InputElementDesc配列を動的に構築
    auto* pInputDesc = new D3D11_INPUT_ELEMENT_DESC[shaderDesc.InputParameters];
    for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
    {
        D3D11_SIGNATURE_PARAMETER_DESC sigDesc;
        pReflection->GetInputParameterDesc(i, &sigDesc);

        pInputDesc[i].SemanticName = sigDesc.SemanticName;
        pInputDesc[i].SemanticIndex = sigDesc.SemanticIndex;

        // Mask → 使用成分数をビットカウントで算出
        BYTE elementCount = sigDesc.Mask;
        elementCount = (elementCount & 0x05) + ((elementCount >> 1) & 0x05);
        elementCount = (elementCount & 0x03) + ((elementCount >> 2) & 0x03);

        // ComponentType → DXGI_FORMAT決定
        switch (sigDesc.ComponentType)
        {
        case D3D_REGISTER_COMPONENT_UINT32:
            pInputDesc[i].Format = formats[0][elementCount - 1];
            break;
        case D3D_REGISTER_COMPONENT_SINT32:
            pInputDesc[i].Format = formats[1][elementCount - 1];
            break;
        case D3D_REGISTER_COMPONENT_FLOAT32:
            pInputDesc[i].Format = formats[2][elementCount - 1];
            break;
        default:
            pInputDesc[i].Format = DXGI_FORMAT_R32_FLOAT;
            break;
        }

        pInputDesc[i].InputSlot = 0;
        pInputDesc[i].AlignedByteOffset =
            (i == 0) ? 0 : D3D11_APPEND_ALIGNED_ELEMENT;
        pInputDesc[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        pInputDesc[i].InstanceDataStepRate = 0;
    }

    hr = device->CreateInputLayout(
        pInputDesc,
        shaderDesc.InputParameters,
        pData, size,
        &m_InputLayout);

    delete[] pInputDesc;

    if (FAILED(hr))
    {
        std::cout << "[Error] CreateInputLayout failed" << std::endl;
    }

    return hr;
}