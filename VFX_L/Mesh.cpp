#include "Mesh.h"

bool Mesh::Create(
    ID3D11Device* device,
    const std::vector<VERTEX_3D>& vertices,
    const std::vector<unsigned int>& indices,
    bool dynamic)
{
    if (!m_VertexBuffer.Create(device, vertices, dynamic))
        return false;

    if (!m_IndexBuffer.Create(device, indices))
        return false;

    return true;
}

void Mesh::Draw(
    ID3D11DeviceContext* context,
    D3D_PRIMITIVE_TOPOLOGY topology)
{
    if (!context)
        return;

    context->IASetPrimitiveTopology(topology);
    m_VertexBuffer.SetGPU(context);
    m_IndexBuffer.SetGPU(context);
    context->DrawIndexed(m_IndexBuffer.GetIndexCount(), 0, 0);
}

bool Mesh::ModifyVertices(
    ID3D11DeviceContext* context,
    const std::vector<VERTEX_3D>& vertices)
{
    return m_VertexBuffer.Modify(context, vertices);
}