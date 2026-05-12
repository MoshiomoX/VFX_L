#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

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

private:
    ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
    ComPtr<ID3D11SamplerState> m_SamplerState;
    int m_Width = 0;
    int m_Height = 0;
};