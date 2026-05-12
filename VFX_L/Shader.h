#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <d3dcompiler.h>


#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;


class Shader
{
public:
    bool Load(ID3D11Device* device, const std::wstring& vsPath, const std::wstring& psPath);
    bool LoadParticle(ID3D11Device* device, const std::wstring& vsPath, const std::wstring& psPath);
    void Bind(ID3D11DeviceContext* context);

private:
    bool CompileShader(const std::wstring& path, const char* entry, const char* target, ID3DBlob** blob);
    bool CreateDefaultLayout(ID3D11Device* device, ID3DBlob* vsBlob);
    bool CreateParticleLayout(ID3D11Device* device, ID3DBlob* vsBlob);

private:
    ComPtr<ID3D11VertexShader> m_VertexShader;
    ComPtr<ID3D11PixelShader> m_PixelShader;
    ComPtr<ID3D11InputLayout> m_InputLayout;
};