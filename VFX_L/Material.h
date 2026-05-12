#pragma once
#include <memory>
#include <d3d11.h>
#include <SimpleMath.h>
#include "Shader.h"
#include "Texture.h"

using namespace DirectX::SimpleMath;

class Material
{
public:
    void SetShader(std::shared_ptr<Shader> shader) { m_Shader = shader; }
    void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }
    void SetColor(const Vector4& color) { m_Color = color; }

    Shader* GetShader() const { return m_Shader.get(); }
    Texture* GetTexture() const { return m_Texture.get(); }
    const Vector4& GetColor() const { return m_Color; }

    bool HasShader() const { return m_Shader != nullptr; }
    bool HasTexture() const { return m_Texture != nullptr; }

    void Bind(ID3D11DeviceContext* context);

private:
    std::shared_ptr<Shader> m_Shader;
    std::shared_ptr<Texture> m_Texture;
    Vector4 m_Color = { 1.0f, 1.0f, 1.0f, 1.0f };
};