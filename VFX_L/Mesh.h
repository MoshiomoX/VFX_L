#pragma once
#include <vector>
#include <d3d11.h>
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "VertexTypes.h"  // VERTEX_3D

class Mesh
{
public:
    bool Create(
        ID3D11Device* device,
        const std::vector<VERTEX_3D>& vertices,
        const std::vector<unsigned int>& indices,
        bool dynamic = false);

    void Draw(
        ID3D11DeviceContext* context,
        D3D_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // í∏ì_èëÇ´ä∑Ç¶ÅidynamicéûÇÃÇ›Åj
    bool ModifyVertices(
        ID3D11DeviceContext* context,
        const std::vector<VERTEX_3D>& vertices);

    UINT GetVertexCount() const { return m_VertexBuffer.GetVertexCount(); }
    UINT GetIndexCount() const { return m_IndexBuffer.GetIndexCount(); }
    bool IsValid() const { return m_VertexBuffer.IsValid(); }

private:
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer<> m_IndexBuffer;
};