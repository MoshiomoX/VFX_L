// ============================================================
// Texture.cpp
// ============================================================
#include "Graphics/Material/Texture.h"
#include <DirectXTex.h>
#include <iostream>

// ============================================================
// ファイルから
// ============================================================
bool Texture::Load(ID3D11Device* device, const std::wstring& filepath)
{
    if (!device) return false;

    DirectX::ScratchImage image;
    HRESULT hr;

    // 拡張子で読み込み経路を分ける
    std::wstring ext;
    const size_t dot = filepath.find_last_of(L'.');
    if (dot != std::wstring::npos) ext = filepath.substr(dot);

    if (ext == L".dds" || ext == L".DDS")
        hr = DirectX::LoadFromDDSFile(filepath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    else if (ext == L".tga" || ext == L".TGA")
        hr = DirectX::LoadFromTGAFile(filepath.c_str(), nullptr, image);
    else if (ext == L".hdr" || ext == L".HDR")
        hr = DirectX::LoadFromHDRFile(filepath.c_str(), nullptr, image);
    else
        hr = DirectX::LoadFromWICFile(filepath.c_str(),
            DirectX::WIC_FLAGS_FORCE_RGB, nullptr, image);


    if (FAILED(hr))
    {
        std::wcout << L"[Error] Texture load failed: " << filepath << std::endl;
        return false;
    }


    return FinishFromImage(device, image, filepath);
}

// ============================================================
// メモリ上の画像ファイルから
// ============================================================
bool Texture::LoadFromMemory(ID3D11Device* device, const void* data, size_t size,
    const char* formatHint)
{
    if (!device || !data || size == 0) return false;

    DirectX::ScratchImage image;
    HRESULT hr = E_FAIL;

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    const std::string hint = formatHint ? formatHint : "";
    if (hint == "dds")
        hr = DirectX::LoadFromDDSMemory(bytes, size, DirectX::DDS_FLAGS_NONE, nullptr, image);
    else if (hint == "tga")
        hr = DirectX::LoadFromTGAMemory(bytes, size, nullptr, image);
    else if (hint == "hdr")
        hr = DirectX::LoadFromHDRMemory(bytes, size, nullptr, image);

    if (FAILED(hr))
        hr = DirectX::LoadFromWICMemory(bytes, size,
            DirectX::WIC_FLAGS_FORCE_RGB, nullptr, image);
    if (FAILED(hr))
    {
        std::cout << "[Error] Texture decode from memory failed (hint: " << hint << ")" << std::endl;
        return false;
    }

    return FinishFromImage(device, image, L"<embedded>");
}

// ============================================================
// 共通部: mipmap → SRV → サイズ控え
// ============================================================
bool Texture::FinishFromImage(ID3D11Device* device, DirectX::ScratchImage& image,
    const std::wstring& nameForLog)
{
    HRESULT hr;
    // ---- 単チャンネル（灰度 png 等）は RGBA に広げる ----
    // R8 のままだと sampler が (r,0,0,1) を返して赤くなる
    {
        const DXGI_FORMAT f = image.GetMetadata().format;
        const bool single =
            f == DXGI_FORMAT_R8_UNORM || f == DXGI_FORMAT_R16_UNORM ||
            f == DXGI_FORMAT_R16_FLOAT || f == DXGI_FORMAT_R32_FLOAT ||
            f == DXGI_FORMAT_A8_UNORM;
        if (single)
        {
            DirectX::ScratchImage gray;
            hr = DirectX::TransformImage(
                image.GetImages(), image.GetImageCount(), image.GetMetadata(),
                [](DirectX::XMVECTOR* out, const DirectX::XMVECTOR* in, size_t w, size_t)
                {
                    for (size_t i = 0; i < w; ++i)
                    {
                        const float r = DirectX::XMVectorGetX(in[i]);
                        out[i] = DirectX::XMVectorSet(r, r, r, 1.0f);
                    }
                }, gray);
            if (SUCCEEDED(hr))
            {
                DirectX::ScratchImage rgba;
                hr = DirectX::Convert(gray.GetImages(), gray.GetImageCount(), gray.GetMetadata(),
                    DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
                    DirectX::TEX_THRESHOLD_DEFAULT, rgba);
                if (SUCCEEDED(hr))
                    image = std::move(rgba);
            }
        }
    }

    // mipmap が無ければ作る。圧縮済み（BC 系）はそのまま使う
    const auto& meta = image.GetMetadata();
    if (meta.mipLevels == 1 && !DirectX::IsCompressed(meta.format))
    {
        DirectX::ScratchImage mipImage;
        hr = DirectX::GenerateMipMaps(
            image.GetImages(), image.GetImageCount(), meta,
            DirectX::TEX_FILTER_DEFAULT, 0, mipImage);
        if (SUCCEEDED(hr))
            image = std::move(mipImage);
    }

    hr = DirectX::CreateShaderResourceView(
        device,
        image.GetImages(), image.GetImageCount(),
        image.GetMetadata(),
        &m_ShaderResourceView);
    if (FAILED(hr))
    {
        std::wcout << L"[Error] SRV creation failed: " << nameForLog << std::endl;
        return false;
    }

    m_Width = (int)image.GetMetadata().width;
    m_Height = (int)image.GetMetadata().height;

    std::wcout << L"[OK] Texture loaded: " << nameForLog
        << L" (" << m_Width << L"x" << m_Height << L")" << std::endl;
    return true;
}

// ============================================================
// 生ピクセルから（mipmap 無し）
// ============================================================
bool Texture::CreateFromMemory(ID3D11Device* device, const void* pixels,
    UINT width, UINT height, DXGI_FORMAT format)
{
    if (!device || !pixels) return false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = pixels;
    init.SysMemPitch = width * 4;   // 4 byte/pixel の形式のみ想定

    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device->CreateTexture2D(&td, &init, &tex))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(tex.Get(), &sd, &m_ShaderResourceView)))
        return false;

    m_Width = (int)width;
    m_Height = (int)height;
    return true;
}

bool Texture::CreateSolid(ID3D11Device* device,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const uint8_t px[4] = { r, g, b, a };
    return CreateFromMemory(device, px, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
}

// ============================================================
// bind
// ============================================================
void Texture::Bind(ID3D11DeviceContext* context, UINT slot)
{
    if (!context) return;
    context->PSSetShaderResources(slot, 1, m_ShaderResourceView.GetAddressOf());

    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    context->PSSetSamplers(slot, 1, &samp);
}

void Texture::Unbind(ID3D11DeviceContext* context, UINT slot)
{
    if (!context) return;
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(slot, 1, &nullSRV);
}