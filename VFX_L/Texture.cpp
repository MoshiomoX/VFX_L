#include "Texture.h"
#include <WICTextureLoader.h>  // DirectXTKから
#include <iostream>

bool Texture::Load(ID3D11Device* device, const std::wstring& filepath)
{
    if (!device)
        return false;

    // WICで画像読み込み
    ComPtr<ID3D11Resource> resource;
    HRESULT hr = DirectX::CreateWICTextureFromFile(
        device,
        filepath.c_str(),
        &resource,
        &m_ShaderResourceView
    );

    if (FAILED(hr))
    {
        std::wcout << L"[Error] Texture load failed: " << filepath << std::endl;
        return false;
    }

    // サイズ取得
    ComPtr<ID3D11Texture2D> texture;
    resource.As(&texture);
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    m_Width = desc.Width;
    m_Height = desc.Height;

    // Sampler作成
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0;
    sd.MaxLOD = D3D11_FLOAT32_MAX;

    hr = device->CreateSamplerState(&sd, &m_SamplerState);
    if (FAILED(hr))
        return false;

    std::wcout << L"[OK] Texture loaded: " << filepath
        << L" (" << m_Width << L"x" << m_Height << L")" << std::endl;
    return true;
}

void Texture::Bind(ID3D11DeviceContext* context, UINT slot)
{
    if (!context)
        return;

    context->PSSetShaderResources(slot, 1, m_ShaderResourceView.GetAddressOf());
    context->PSSetSamplers(slot, 1, m_SamplerState.GetAddressOf());
}

void Texture::Unbind(ID3D11DeviceContext* context, UINT slot)
{
    if (!context)
        return;

    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(slot, 1, &nullSRV);
}