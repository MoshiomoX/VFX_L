#pragma once
#include <memory>
#include <d3d11.h>
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

class VertexShader;
class PixelShader;
class Texture;

// ============================================
// マテリアル
// VertexShader + PixelShader + Texture を統合管理
// ============================================
class Material
{
public:
    void SetVertexShader(std::shared_ptr<VertexShader> vs) { m_VS = vs; }
    void SetPixelShader(std::shared_ptr<PixelShader> ps) { m_PS = ps; }
    void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }
    void SetColor(const Vector4& color) { m_Color = color; }

    VertexShader* GetVS() const { return m_VS.get(); }
    PixelShader* GetPS() const { return m_PS.get(); }
    Texture* GetTexture() const { return m_Texture.get(); }
    const Vector4& GetColor() const { return m_Color; }

    bool HasVS() const { return m_VS != nullptr; }
    bool HasPS() const { return m_PS != nullptr; }
    bool HasTexture() const { return m_Texture != nullptr; }

    void Bind(ID3D11DeviceContext* context);

private:
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader> m_PS;
    std::shared_ptr<Texture> m_Texture;
    Vector4 m_Color = { 1.0f, 1.0f, 1.0f, 1.0f };
};