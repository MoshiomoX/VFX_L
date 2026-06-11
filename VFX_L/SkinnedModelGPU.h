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

// CSが出力する頂点（描画VSが読む）。HLSL側structと厳密一致(48byte)
struct SkinnedVertexOut
{
    DirectX::SimpleMath::Vector3 position; float _pad0;
    DirectX::SimpleMath::Vector3 normal;   float _pad1;
    DirectX::SimpleMath::Vector2 uv;       float _pad2[2];
};

class SkinnedModelGPU
{
public:
    // submesh 1つ分のGPUリソース
    struct GpuSubMesh
    {
        UINT vertexCount = 0;
        UINT indexCount = 0;
        int  materialIndex = -1;

        ComPtr<ID3D11Buffer>              bindBuffer;   // 入力: bind頂点
        ComPtr<ID3D11ShaderResourceView>  bindSRV;

        ComPtr<ID3D11Buffer>              skinnedBuffer; // 出力: skinning結果
        ComPtr<ID3D11UnorderedAccessView> skinnedUAV;    // CSが書く
        ComPtr<ID3D11ShaderResourceView>  skinnedSRV;     // VSが読む

        ComPtr<ID3D11Buffer>              indexBuffer;    // DrawIndexed用
    };

    bool Initialize(ID3D11Device* device, const SkinnedModel& model);
    void Render(ID3D11DeviceContext* ctx, VertexShader* vs, PixelShader* ps,
        const DirectX::SimpleMath::Matrix& world,
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj);
    const std::vector<GpuSubMesh>& GetSubMeshes() const { return m_SubMeshes; }
    ID3D11ShaderResourceView* GetPaletteSRV() const { return m_PaletteSRV.Get(); }
    ID3D11Buffer* GetPaletteBuffer() const { return m_PaletteBuffer.Get(); }
    UINT GetBoneCount() const { return m_BoneCount; }

	void UploadIdentityPalette(ID3D11DeviceContext* ctx);
    void UploadPalette(ID3D11DeviceContext* ctx,
        const std::vector<DirectX::SimpleMath::Matrix>& palette);
    void Skin(ID3D11DeviceContext* ctx,ComputeShader* cs);
private:
    bool CreateSubMeshBuffers(ID3D11Device* device, const SkinnedModel::SubMesh& src);
    bool CreatePaletteBuffer(ID3D11Device* device, UINT boneCount);

    std::vector<GpuSubMesh> m_SubMeshes;

    // 全submesh共有：ボーン行列パレット（毎フレーム更新, B6）
    ComPtr<ID3D11Buffer>             m_PaletteBuffer;
    ComPtr<ID3D11ShaderResourceView> m_PaletteSRV;
    UINT m_BoneCount = 0;
};