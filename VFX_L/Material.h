#pragma once
#include <memory>
#include <d3d11.h>
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

class VertexShader;
class PixelShader;
class Texture;

class Material
{
public:
    enum TextureSlot
    {
        Albedo = 0,
        Normal = 1,
        Metallic = 2,
        Roughness = 3,
        AO = 4,
        SlotCount   // = 5
    };

    void SetVertexShader(std::shared_ptr<VertexShader> vs) { m_VS = vs; }
    void SetPixelShader(std::shared_ptr<PixelShader> ps) { m_PS = ps; }
    VertexShader* GetVS() const { return m_VS.get(); }
    PixelShader* GetPS() const { return m_PS.get(); }
    bool HasVS() const { return m_VS != nullptr; }
    bool HasPS() const { return m_PS != nullptr; }

    void SetAlbedoTexture(std::shared_ptr<Texture> t) { m_Textures[Albedo] = t; }
    void SetNormalTexture(std::shared_ptr<Texture> t) { m_Textures[Normal] = t; }
    void SetMetallicTexture(std::shared_ptr<Texture> t) { m_Textures[Metallic] = t; }
    void SetRoughnessTexture(std::shared_ptr<Texture> t) { m_Textures[Roughness] = t; }
    void SetAOTexture(std::shared_ptr<Texture> t) { m_Textures[AO] = t; }

    Texture* GetAlbedoTexture() const { return m_Textures[Albedo].get(); }
    Texture* GetNormalTexture() const { return m_Textures[Normal].get(); }
    Texture* GetMetallicTexture() const { return m_Textures[Metallic].get(); }
    Texture* GetRoughnessTexture() const { return m_Textures[Roughness].get(); }
    Texture* GetAOTexture() const { return m_Textures[AO].get(); }

    void SetTextureSlot(TextureSlot slot, std::shared_ptr<Texture> t) { m_Textures[slot] = t; }

    // 後方互換
    void SetTexture(std::shared_ptr<Texture> t) { m_Textures[Albedo] = t; }
    Texture* GetTexture() const { return m_Textures[Albedo].get(); }
    bool HasTexture() const { return m_Textures[Albedo] != nullptr; }

    void SetColor(const Vector4& color) { m_Color = color; }
    const Vector4& GetColor() const { return m_Color; }

    void Bind(ID3D11DeviceContext* context);
    static void InitDefaultTextures(ID3D11Device* device);
private:
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader> m_PS;
    std::shared_ptr<Texture> m_Textures[SlotCount];
    static std::shared_ptr<Texture> s_DefaultTextures[SlotCount];
    Vector4 m_Color = { 1, 1, 1, 1 };
};