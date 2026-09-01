#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include "RenderStates.h"
using Microsoft::WRL::ComPtr;

class Texture
{
public:
    bool Load(ID3D11Device* device, const std::wstring& filepath);
    void Bind(ID3D11DeviceContext* context, UINT slot = 0);
    void Unbind(ID3D11DeviceContext* context, UINT slot = 0);
    bool IsValid() const { return m_ShaderResourceView != nullptr; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    ID3D11ShaderResourceView* GetSRV() const { return m_ShaderResourceView.Get(); }



    // ※メモリ上のピクセルからテクスチャ生成（プレースホルダ用）
    bool CreateFromMemory(ID3D11Device* device, const void* pixels,
        UINT width, UINT height,
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

    // ※1x1 単色テクスチャ生成（RGBA各0-255）
    bool CreateSolid(ID3D11Device* device,
        uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);


private:
    ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
    ComPtr<ID3D11SamplerState> m_SamplerState;
    int m_Width = 0;
    int m_Height = 0;
};