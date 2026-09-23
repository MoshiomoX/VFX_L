#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <SimpleMath.h>
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Light/LightTypes.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"    

using Microsoft::WRL::ComPtr;

struct SkinnedVertexOut
{
    DirectX::SimpleMath::Vector3 position; float _pad0;
    DirectX::SimpleMath::Vector3 normal;   float _pad1;
    DirectX::SimpleMath::Vector3 tangent;  float _pad2;
    DirectX::SimpleMath::Vector2 uv;       float _pad3[2];
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

        // 粒子の Mesh 発射源用 raw view。
        // skinnedBuffer は STRUCTURED なので ALLOW_RAW_VIEWS と同居できない。
        // 同サイズの raw な双子を持ち、SkinSubmesh の末尾で CopyResource する
        // （emitSourceEnabled の submesh だけ。48B × 頂点数のコピー）
        ComPtr<ID3D11Buffer>              emitRawBuffer;
        ComPtr<ID3D11ShaderResourceView>  emitRawSRV;
        bool emitSourceEnabled = false;

        ComPtr<ID3D11Buffer>              indexBuffer;
        ComPtr<ID3D11ShaderResourceView>  indexRawSRV;   // 粒子の三角形発射用（R32_UINT の raw view）

        bool visible = true;
    };

    bool Initialize(ID3D11DeviceContext* ctx, ID3D11Device* device, const SkinnedModel& model);

    // 新增：对单个 submesh 进行 skinning（上传该 submesh 专用的 palette）
    void SkinSubmesh(ID3D11DeviceContext* ctx, ComputeShader* cs, int submeshIndex,
        const std::vector<DirectX::SimpleMath::Matrix>& palette);

    void Render(ID3D11DeviceContext* ctx, const SkinnedModel& model,
        const LightBuffer& light,
        const DirectX::SimpleMath::Matrix& world,
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj);

    const std::vector<GpuSubMesh>& GetSubMeshes() const { return m_SubMeshes; }
    UINT GetBoneCount() const { return m_BoneCount; }

    void UploadIdentityPalette(ID3D11DeviceContext* ctx);
    void UploadPalette(ID3D11DeviceContext* ctx, const std::vector<DirectX::SimpleMath::Matrix>& palette);

    void SetSubMeshVisible(int index, bool visible)
    {
        if (index >= 0 && index < (int)m_SubMeshes.size()) m_SubMeshes[index].visible = visible;
    }

    // ---- 粒子の Mesh 発射源 ----
    // GPUParticleSystem::RegisterEmitSource(GetSkinnedRawSRV(i), GetSubMeshVertexCount(i), kLayoutSkinned)
    // 登録したら SetEmitSourceEnabled(i, true) で毎フレームの複写を有効にする
    ID3D11ShaderResourceView* GetSkinnedRawSRV(int index) const
    {
        return (index >= 0 && index < (int)m_SubMeshes.size()) ? m_SubMeshes[index].emitRawSRV.Get() : nullptr;
    }
    UINT GetSubMeshVertexCount(int index) const
    {
        return (index >= 0 && index < (int)m_SubMeshes.size()) ? m_SubMeshes[index].vertexCount : 0;
    }
    ID3D11ShaderResourceView* GetIndexRawSRV(int index) const
    {
        return (index >= 0 && index < (int)m_SubMeshes.size()) ? m_SubMeshes[index].indexRawSRV.Get() : nullptr;
    }
    UINT GetSubMeshIndexCount(int index) const
    {
        return (index >= 0 && index < (int)m_SubMeshes.size()) ? m_SubMeshes[index].indexCount : 0;
    }
    void SetEmitSourceEnabled(int index, bool enabled)
    {
        if (index >= 0 && index < (int)m_SubMeshes.size()) m_SubMeshes[index].emitSourceEnabled = enabled;
    }
    bool IsSubMeshVisible(int index) const
    {
        return (index >= 0 && index < (int)m_SubMeshes.size()) ? m_SubMeshes[index].visible : false;
    }

private:
    bool CreateSubMeshBuffers(ID3D11Device* device, const SkinnedModel::SubMesh& src);
    bool CreatePaletteBuffer(ID3D11Device* device, UINT boneCount);

    std::vector<GpuSubMesh> m_SubMeshes;

    ComPtr<ID3D11Buffer>             m_PaletteBuffer;
    ComPtr<ID3D11ShaderResourceView> m_PaletteSRV;
    UINT m_BoneCount = 0;
};