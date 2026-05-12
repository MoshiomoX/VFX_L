#include "MeshRenderer.h"
#include "Renderer.h"
#include "GameObject.h"

void MeshRenderer::Draw(Renderer& renderer)
{
    if (!m_Mesh || !m_Owner)
        return;

    renderer.DrawMesh(m_Mesh, &m_Owner->GetTransform(), m_Material);
}