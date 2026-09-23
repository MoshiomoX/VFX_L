#pragma once
#include <vector>
#include <d3d11.h>
#include "Graphics/Mesh/VertexBuffer.h"
#include "Graphics/Mesh/IndexBuffer.h"
#include "Graphics/Mesh/Vertex3D.h"  // VERTEX_3D

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

    void DrawInstanced(ID3D11DeviceContext* context, UINT instanceCount,
        D3D_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // instance 数を GPU が決める描画（粒子の立方体用）。
    // args は DrawIndexedInstancedIndirect の 5 uint（IndexCountPerInstance は呼び出し側が入れる）
    void DrawIndexedInstancedIndirect(ID3D11DeviceContext* context,
        ID3D11Buffer* args, UINT argsOffset,
        D3D_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // 頂点書き換え（dynamic時のみ）
    bool ModifyVertices(
        ID3D11DeviceContext* context,
        const std::vector<VERTEX_3D>& vertices);

    UINT GetVertexCount() const { return m_VertexBuffer.GetVertexCount(); }
    UINT GetIndexCount() const { return m_IndexBuffer.GetIndexCount(); }
    bool IsValid() const { return m_VertexBuffer.IsValid(); }

    // 粒子の Mesh 発射源用。頂点バッファの raw SRV（ByteAddressBuffer）
    // GPUParticleSystem::RegisterEmitSource に渡す。レイアウトは VERTEX_3D
    ID3D11ShaderResourceView* GetVertexSRV() const { return m_VertexBuffer.GetRawSRV(); }
    // 同じく index buffer の raw SRV（三角形を選ぶ用）と 1 index のバイト数
    ID3D11ShaderResourceView* GetIndexSRV() const { return m_IndexBuffer.GetRawSRV(); }
    UINT GetIndexBytes() const { return IndexBuffer<>::GetIndexBytes(); }

private:
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer<> m_IndexBuffer;
};