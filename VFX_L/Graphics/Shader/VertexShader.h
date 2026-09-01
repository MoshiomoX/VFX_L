#pragma once
#include "Graphics/Shader/Shader.h"

// ============================================
// 頂点シェーダー
// Reflectionから自動InputLayout生成
// ============================================
class VertexShader : public Shader
{
public:
    VertexShader();
    ~VertexShader() override = default;

    void Bind(ID3D11DeviceContext* context) override;
    void Unbind(ID3D11DeviceContext* context) override;

protected:
    HRESULT MakeShader(ID3D11Device* device,
        void* pData, UINT size) override;

private:
    ComPtr<ID3D11VertexShader> m_VS;
    ComPtr<ID3D11InputLayout> m_InputLayout;
};