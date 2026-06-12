#include "SkinnedModelGPU.h"
#include <iostream>

using namespace DirectX::SimpleMath;

bool SkinnedModelGPU::CreateSubMeshBuffers(ID3D11Device* device, const SkinnedModel::SubMesh& src)
{
    GpuSubMesh gm;
    gm.vertexCount = (UINT)src.vertices.size();
    gm.indexCount = (UINT)src.indices.size();
    gm.materialIndex = src.materialIndex;

    if (gm.vertexCount == 0 || gm.indexCount == 0)
        return true;

    // 1. bind顶点
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(SkinnedVertex) * gm.vertexCount;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(SkinnedVertex);

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = src.vertices.data();
        if (FAILED(device->CreateBuffer(&bd, &init, &gm.bindBuffer))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = gm.vertexCount;
        if (FAILED(device->CreateShaderResourceView(gm.bindBuffer.Get(), &sd, &gm.bindSRV))) return false;
    }

    // 2. skinned 输出
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(SkinnedVertexOut) * gm.vertexCount;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(SkinnedVertexOut);
        if (FAILED(device->CreateBuffer(&bd, nullptr, &gm.skinnedBuffer))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = gm.vertexCount;
        if (FAILED(device->CreateUnorderedAccessView(gm.skinnedBuffer.Get(), &ud, &gm.skinnedUAV))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = gm.vertexCount;
        if (FAILED(device->CreateShaderResourceView(gm.skinnedBuffer.Get(), &sd, &gm.skinnedSRV))) return false;
    }

    // 3. index buffer
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(uint32_t) * gm.indexCount;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = src.indices.data();
        if (FAILED(device->CreateBuffer(&bd, &init, &gm.indexBuffer))) return false;
    }

    m_SubMeshes.push_back(std::move(gm));
    return true;
}

bool SkinnedModelGPU::CreatePaletteBuffer(ID3D11Device* device, UINT boneCount)
{
    m_BoneCount = boneCount;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(Matrix) * boneCount;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Matrix);

    if (FAILED(device->CreateBuffer(&bd, nullptr, &m_PaletteBuffer))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = boneCount;
    if (FAILED(device->CreateShaderResourceView(m_PaletteBuffer.Get(), &sd, &m_PaletteSRV))) return false;

    return true;
}

bool SkinnedModelGPU::Initialize(ID3D11DeviceContext* ctx, ID3D11Device* device, const SkinnedModel& model)
{
    if (!device) return false;

    for (const auto& sub : model.GetSubMeshes())
        if (!CreateSubMeshBuffers(device, sub))
            return false;

    if (!CreatePaletteBuffer(device, (UINT)model.GetSkeleton().GetBoneCount()))
        return false;

    UploadIdentityPalette(ctx);

    std::cout << "[SkinnedModelGPU] OK: GpuSubMesh=" << m_SubMeshes.size()
        << " Bone=" << m_BoneCount << std::endl;
    return true;
}

void SkinnedModelGPU::UploadIdentityPalette(ID3D11DeviceContext* ctx)
{
    if (!ctx || m_BoneCount == 0) return;
    std::vector<Matrix> palette(m_BoneCount, Matrix::Identity);
    UploadPalette(ctx, palette);
}

void SkinnedModelGPU::UploadPalette(ID3D11DeviceContext* ctx, const std::vector<Matrix>& palette)
{
    if (!ctx || palette.empty()) return;
    const size_t count = min((size_t)m_BoneCount, palette.size());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(ctx->Map(m_PaletteBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, palette.data(), sizeof(Matrix) * count);
        ctx->Unmap(m_PaletteBuffer.Get(), 0);
    }
}

// 新增：单个 submesh skinning
void SkinnedModelGPU::SkinSubmesh(ID3D11DeviceContext* ctx, ComputeShader* cs, int submeshIndex,
    const std::vector<Matrix>& palette)
{
    if (!ctx || !cs || submeshIndex < 0 || submeshIndex >= (int)m_SubMeshes.size())
        return;

    UploadPalette(ctx, palette);   // 上传当前 submesh 专用的 palette

    auto& gm = m_SubMeshes[submeshIndex];

    cs->Bind(ctx);

    // CB b0: 顶点数
    struct { UINT vertexCount; UINT pad[3]; } cb{ gm.vertexCount, {0,0,0} };
    cs->WriteBuffer(ctx, 0, &cb);

    ctx->CSSetShaderResources(0, 1, gm.bindSRV.GetAddressOf());     // t0: bind verts
    ctx->CSSetShaderResources(1, 1, m_PaletteSRV.GetAddressOf());   // t1: palette

    ID3D11UnorderedAccessView* uav = gm.skinnedUAV.Get();
    ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

    ctx->Dispatch((gm.vertexCount + 255) / 256, 1, 1);

    // 解绑
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    ctx->CSSetShaderResources(0, 2, nullSRV);
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

void SkinnedModelGPU::Render(ID3D11DeviceContext* ctx, VertexShader* vs, PixelShader* ps,
    const Matrix& world, const Matrix& view, const Matrix& proj)
{
    if (!ctx || !vs || !ps) return;

    struct { Matrix W, V, P; } cb{ world, view, proj };
    vs->WriteBuffer(ctx, 0, &cb);

    vs->Bind(ctx);
    ps->Bind(ctx);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ID3D11Buffer* nullVB = nullptr; UINT s = 0, o = 0;
    ctx->IASetVertexBuffers(0, 1, &nullVB, &s, &o);

    for (auto& gm : m_SubMeshes)
    {
        ctx->VSSetShaderResources(0, 1, gm.skinnedSRV.GetAddressOf());
        ctx->IASetIndexBuffer(gm.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx->DrawIndexed(gm.indexCount, 0, 0);
    }

    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->VSSetShaderResources(0, 1, &nullSRV);
}