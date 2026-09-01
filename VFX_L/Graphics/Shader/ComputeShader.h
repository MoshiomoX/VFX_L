#pragma once
#include "Graphics/Shader/Shader.h"

// ============================================
// コンピュートシェーダー
// ============================================
class ComputeShader : public Shader
{
public:
    ComputeShader();
    ~ComputeShader() override = default;

    void Bind(ID3D11DeviceContext* context) override;
    void Unbind(ID3D11DeviceContext* context) override;

protected:
    HRESULT MakeShader(ID3D11Device* device,
        void* pData, UINT size) override;

private:
    ComPtr<ID3D11ComputeShader> m_CS;
};