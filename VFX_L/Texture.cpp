#include "Texture.h"
#include <DirectXTex.h>
#include <iostream>

bool Texture::Load(ID3D11Device* device, const std::wstring& filepath)
{
    if (!device)
        return false;

    DirectX::ScratchImage image;
    HRESULT hr;

    // 拡張子で読み込み方法を分岐
    std::wstring ext = filepath.substr(filepath.find_last_of(L'.'));

    if (ext == L".dds" || ext == L".DDS")
    {
        hr = DirectX::LoadFromDDSFile(filepath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    }
    else if (ext == L".tga" || ext == L".TGA")
    {
        hr = DirectX::LoadFromTGAFile(filepath.c_str(), nullptr, image);
    }
    else if (ext == L".hdr" || ext == L".HDR")
    {
        hr = DirectX::LoadFromHDRFile(filepath.c_str(), nullptr, image);
    }
    else
    {
        // PNG, JPG, BMP等
        hr = DirectX::LoadFromWICFile(filepath.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image);
    }

    if (FAILED(hr))
    {
        std::wcout << L"[Error] Texture load failed: " << filepath << std::endl;
        return false;
    }

    // ミップマップ生成（DDS以外で1レベルの場合）
    DirectX::ScratchImage mipImage;
    if (image.GetMetadata().mipLevels == 1 && image.GetMetadata().format != DXGI_FORMAT_BC1_UNORM)
    {
        hr = DirectX::GenerateMipMaps(
            image.GetImages(), image.GetImageCount(),
            image.GetMetadata(),
            DirectX::TEX_FILTER_DEFAULT, 0,
            mipImage);

        if (SUCCEEDED(hr))
        {
            image = std::move(mipImage);
        }
    }

    // SRV作成
    hr = DirectX::CreateShaderResourceView(
        device,
        image.GetImages(), image.GetImageCount(),
        image.GetMetadata(),
        &m_ShaderResourceView);

    if (FAILED(hr))
    {
        std::wcout << L"[Error] SRV creation failed: " << filepath << std::endl;
        return false;
    }

    // サイズ取得
    m_Width = static_cast<int>(image.GetMetadata().width);
    m_Height = static_cast<int>(image.GetMetadata().height);

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
    if (!context) return;
    context->PSSetShaderResources(slot, 1, m_ShaderResourceView.GetAddressOf());
    context->PSSetSamplers(slot, 1, m_SamplerState.GetAddressOf());
}

void Texture::Unbind(ID3D11DeviceContext* context, UINT slot)
{
    if (!context) return;
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(slot, 1, &nullSRV);
}