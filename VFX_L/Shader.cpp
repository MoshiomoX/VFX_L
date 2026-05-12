#include "Shader.h"
#include <iostream>


bool Shader::Load(ID3D11Device* device, const std::wstring& vsPath, const std::wstring& psPath)
{
    // VS
    ComPtr<ID3DBlob> vsBlob;
    if (!CompileShader(vsPath, "main", "vs_5_0", &vsBlob))
        return false;

    HRESULT hr = device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &m_VertexShader);

    if (FAILED(hr))
    {
        std::cout << "[Error] Failed to create vertex shader" << std::endl;
        return false;
    }

    // PS
    ComPtr<ID3DBlob> psBlob;
    if (!CompileShader(psPath, "main", "ps_5_0", &psBlob))
        return false;

    hr = device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        &m_PixelShader);

    if (FAILED(hr))
    {
        std::cout << "[Error] Failed to create pixel shader" << std::endl;
        return false;
    }

    // InputLayout
    if (!CreateDefaultLayout(device, vsBlob.Get()))
        return false;

    std::cout << "[OK] Shader loaded" << std::endl;
    return true;
}

bool Shader::LoadParticle(ID3D11Device* device, const std::wstring& vsPath, const std::wstring& psPath)
{
    // VS
    ComPtr<ID3DBlob> vsBlob;
    if (!CompileShader(vsPath, "main", "vs_5_0", &vsBlob))
        return false;

    HRESULT hr = device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &m_VertexShader);

    if (FAILED(hr))
    {
        std::cout << "[Error] Failed to create particle vertex shader" << std::endl;
        return false;
    }

    // PS
    ComPtr<ID3DBlob> psBlob;
    if (!CompileShader(psPath, "main", "ps_5_0", &psBlob))
        return false;

    hr = device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        &m_PixelShader);

    if (FAILED(hr))
    {
        std::cout << "[Error] Failed to create particle pixel shader" << std::endl;
        return false;
    }

    // Particle InputLayout
    if (!CreateParticleLayout(device, vsBlob.Get()))
        return false;

    std::cout << "[OK] Particle shader loaded" << std::endl;
    return true;
}

bool Shader::CompileShader(const std::wstring& path, const char* entry, const char* target, ID3DBlob** blob)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
    flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(
        path.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry,
        target,
        flags,
        0,
        blob,
        &errorBlob);

    if (FAILED(hr))
    {
        std::wcout << L"[Error] Shader compile failed: " << path << std::endl;

        if (errorBlob)
        {
            std::cout << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    return true;
}

bool Shader::CreateDefaultLayout(ID3D11Device* device, ID3DBlob* vsBlob)
{
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    HRESULT hr = device->CreateInputLayout(
        layout,
        ARRAYSIZE(layout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &m_InputLayout);

    if (FAILED(hr))
    {
        std::cout << "[Error] Failed to create default input layout" << std::endl;
        return false;
    }

    return true;
}

bool Shader::CreateParticleLayout(ID3D11Device* device, ID3DBlob* vsBlob)
{
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "SIZE", 0, DXGI_FORMAT_R32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    HRESULT hr = device->CreateInputLayout(
        layout,
        ARRAYSIZE(layout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &m_InputLayout);

    if (FAILED(hr))
    {
        std::cout << "[Error] Failed to create particle input layout" << std::endl;
        return false;
    }

    return true;
}

void Shader::Bind(ID3D11DeviceContext* context)
{
    context->IASetInputLayout(m_InputLayout.Get());
    context->VSSetShader(m_VertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_PixelShader.Get(), nullptr, 0);
}