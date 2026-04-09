#include "Material.h"
#include <iostream>
void Material::Bind(ID3D11DeviceContext* context)
{
    if (!context)
        return;

    if (m_Shader)
    {
        m_Shader->Bind(context);
    }


    if (m_Texture)
    {
        m_Texture->Bind(context, 0);
    }

}