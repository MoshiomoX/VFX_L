#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <SimpleMath.h>
#include "SkinnedModel.h"
#include "ComputeShader.h"
#include "VertexShader.h"
#include "PixelShader.h"    

using Microsoft::WRL::ComPtr;

struct SkinnedVertexOut
{
    DirectX::SimpleMath::Vector3 position; float _pad0;
    DirectX::SimpleMath::Vector3 normal;   float _pad1;
    DirectX::SimpleMath::Vector2 uv;       float _pad2[2];
};

class SkinnedModelGPU
{
public:
    struct GpuSubMesh
    {
        UINT vertexCount = 0;
        UINT indexCount = 0;
        int  materialIndex = -1;

        ComPtr<ID3D11Buffer>              bindBuffer;
        ComPtr<ID3D11ShaderResourceView>  bindSRV;

        ComPtr<ID3D11Buffer>              skinnedBuffer;
        ComPtr<ID3D11UnorderedAccessView> skinnedUAV;
        ComPtr<ID3D11ShaderResourceView>  skinnedSRV;

        ComPtr<ID3D11Buffer>              indexBuffer;
    };

    bool Initialize(ID3D11DeviceContext* ctx, ID3D11Device* device, const SkinnedModel& model);

    // 新增：对单个 submesh 进行 skinning（上传该 submesh 专用的 palette）
    void SkinSubmesh(ID3D11DeviceContext* ctx, ComputeShader* cs, int submeshIndex,
        const std::vector<DirectX::SimpleMath::Matrix>& palette);

    void Render(ID3D11DeviceContext* ctx, VertexShader* vs, PixelShader* ps,
        const DirectX::SimpleMath::Matrix& world,
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj);

    const std::vector<GpuSubMesh>& GetSubMeshes() const { return m_SubMeshes; }
    UINT GetBoneCount() const { return m_BoneCount; }

    void UploadIdentityPalette(ID3D11DeviceContext* ctx);
    void UploadPalette(ID3D11DeviceContext* ctx, const std::vector<DirectX::SimpleMath::Matrix>& palette);

private:
    bool CreateSubMeshBuffers(ID3D11Device* device, const SkinnedModel::SubMesh& src);
    bool CreatePaletteBuffer(ID3D11Device* device, UINT boneCount);

    std::vector<GpuSubMesh> m_SubMeshes;

    ComPtr<ID3D11Buffer>             m_PaletteBuffer;
    ComPtr<ID3D11ShaderResourceView> m_PaletteSRV;
    UINT m_BoneCount = 0;
};