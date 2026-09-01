#include "Component/ModelRenderer.h"
#include "Graphics/Renderer/Renderer.h"
#include "Object/GameObject.h"

void ModelRenderer::Draw(Renderer& renderer)
{
    if (!m_Model || !m_Owner)
        return;

    m_Model->Draw(renderer, &m_Owner->GetTransform());
}