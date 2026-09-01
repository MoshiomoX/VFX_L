#include "Graphics/Material/Material.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"

std::shared_ptr<Texture> Material::s_DefaultTextures[Material::SlotCount];

void Material::InitDefaultTextures(ID3D11Device* device)
{
    if (s_DefaultTextures[Albedo]) return;   // ?????????????

    auto make = [&](uint8_t r, uint8_t g, uint8_t b)
        {
            auto t = std::make_shared<Texture>();
            t->CreateSolid(device, r, g, b, 255);
            return t;
        };

    // ?slot?????????????(?????)
    s_DefaultTextures[Albedo] = make(255, 255, 255);  // ?:??????
    s_DefaultTextures[Normal] = make(128, 128, 255);  // ???? (0,0,1)
    s_DefaultTextures[Metallic] = make(0, 0, 0);    // ???
    s_DefaultTextures[Roughness] = make(255, 255, 255);  // ???(??????)
    s_DefaultTextures[AO] = make(255, 255, 255);  // ????(????????)
}
void Material::Bind(ID3D11DeviceContext* context)
{
    if (!context) return;

    if (m_VS) m_VS->Bind(context);
    if (m_PS) m_PS->Bind(context);

    if (m_PS)
    {
        for (int slot = 0; slot < SlotCount; ++slot)
        {
            // ????????slot??????(?/????/???)????
            Texture* tex = m_Textures[slot]
                ? m_Textures[slot].get()
                : s_DefaultTextures[slot].get();
            m_PS->SetTexture(context, slot, tex);
        }
    }
}
