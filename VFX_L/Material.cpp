#include "Material.h"
#include "VertexShader.h"
#include "PixelShader.h"
#include "Texture.h"

void Material::Bind(ID3D11DeviceContext* context)
{
    if (!context) return;

    if (m_VS) m_VS->Bind(context);
    if (m_PS) m_PS->Bind(context);

    if (m_PS)
    {
        for (int slot = 0; slot < SlotCount; ++slot)
            m_PS->SetTexture(context, slot, m_Textures[slot].get());
    }
}