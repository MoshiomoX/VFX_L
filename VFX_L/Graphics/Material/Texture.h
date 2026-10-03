#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdint>
#include "Graphics/Renderer/RenderStates.h"

using Microsoft::WRL::ComPtr;

namespace DirectX { class ScratchImage; }

// ============================================================
// Texture
// ファイル / メモリ上の画像 / 生ピクセル の3経路で SRV を作る。
// sampler は持たない（RenderStates の共有物を使う）
// ============================================================
class Texture
{
public:
    // ファイルから（dds / tga / hdr / それ以外は WIC）。
    // srgb = true: WIC の画像を *_SRGB 形式で作り、サンプラーに線形へ戻させる
    // （共有キャッシュを通さない専用の絵だけ。キャッシュの絵は UNORM のまま）
    bool Load(ID3D11Device* device, const std::wstring& filepath, bool srgb = false);

    // 複数の画像を 1 つの Texture2DArray に（2026-10-03、地形の貼図）。1 枚目の大きさに揃え、
    // RGBA8 UNORM・mipmap 付き。シェーダーは Texture2DArray で読む（sRGB の解き方は使う側の PS）
    // outAverage: 各層の平均色（一番小さい mip の 1 画素、0..1、ファイルの値のまま = sRGB の絵なら sRGB）
    bool LoadArray(ID3D11Device* device, const std::vector<std::wstring>& files,
        std::vector<DirectX::XMFLOAT4>* outAverage = nullptr);

    // メモリ上の画像ファイル（png/jpg/dds/tga のバイト列）から。
    // FBX / GLB の埋め込みテクスチャ用。formatHint は assimp の achFormatHint
    bool LoadFromMemory(ID3D11Device* device, const void* data, size_t size,
        const char* formatHint = nullptr);

    // 生ピクセルから（mipmap 無し。埋め込みの非圧縮 BGRA や 1x1 の既定色用）
    bool CreateFromMemory(ID3D11Device* device, const void* pixels,
        UINT width, UINT height,
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

    // 1x1 の単色（RGBA 0-255）
    bool CreateSolid(ID3D11Device* device,
        uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    void Bind(ID3D11DeviceContext* context, UINT slot = 0);
    void Unbind(ID3D11DeviceContext* context, UINT slot = 0);

    bool IsValid() const { return m_ShaderResourceView != nullptr; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    ID3D11ShaderResourceView* GetSRV() const { return m_ShaderResourceView.Get(); }
    // 外で作った SRV を引き取る（NoiseGenCS の出力など）
    void Adopt(ID3D11ShaderResourceView* srv, int width, int height)
    {
        m_ShaderResourceView = srv;
        m_Width = width;
        m_Height = height;
    }
private:
    // Load / LoadFromMemory の共通部: mipmap 生成 → SRV → サイズ控え
    bool FinishFromImage(ID3D11Device* device, DirectX::ScratchImage& image,
        const std::wstring& nameForLog);

    ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
    int m_Width = 0;
    int m_Height = 0;
};