#pragma once
#include "Graphics/Shader/Shader.h"

// ============================================
// ピクセルシェーダー
// ============================================
class PixelShader : public Shader
{
public:
    PixelShader();
    ~PixelShader() override = default;

    void Bind(ID3D11DeviceContext* context) override;
    void Unbind(ID3D11DeviceContext* context) override;

protected:
    HRESULT MakeShader(ID3D11Device* device,
        void* pData, UINT size) override;

private:
    ComPtr<ID3D11PixelShader> m_PS;
};