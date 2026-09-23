#pragma once
#include <d3d11.h>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include "Graphics/Mesh/SkinnedVertex.h"
#include "Graphics/Model/Skeleton.h"

struct aiScene;
class Material;

// ---- アニメのキーフレーム ----
struct VectorKey { float time; DirectX::SimpleMath::Vector3 value; };
struct QuatKey { float time; DirectX::SimpleMath::Quaternion value; };

// 1ノード分のアニメチャンネル
struct BoneChannel
{
    std::string            nodeName;
    std::vector<VectorKey> positions;
    std::vector<QuatKey>   rotations;
    std::vector<VectorKey> scales;
};

// 1つのアニメクリップ
struct AnimationClip
{
    std::string                           name;
    float                                 duration = 0.0f;        // ticks
    float                                 ticksPerSecond = 25.0f;
    std::vector<BoneChannel>              channels;
    std::unordered_map<std::string, int>  nodeToChannel;          // node名 → channel index
};

// ============================================================
// SkinnedModel
// 骨付きモデルの CPU 側データ: bind pose 頂点 / 骨格 / アニメ / 材質。
// 描画用 GPU バッファは SkinnedModelGPU が持つ
// ============================================================
class SkinnedModel
{
public:
    // 1 submesh 分の CPU データ（bind pose）
    struct SubMesh
    {
        std::string                name;
        std::vector<SkinnedVertex> vertices;   // bind pose（mesh 空間、変換は焼かない）
        std::vector<uint32_t>      indices;
        int                        materialIndex = -1;

        // submesh 毎の offset 行列（同じ骨でも submesh によって違い得る）
        std::unordered_map<int, DirectX::SimpleMath::Matrix> boneOffsets;
    };

    bool Load(ID3D11Device* device, const std::string& filepath);
    bool LoadFromScene(ID3D11Device* device, const aiScene* scene,
        const std::string& directory, const std::string& modelName);

    bool  HasAnimation() const { return !m_Animations.empty(); }
    int   GetClipCount() const { return (int)m_Animations.size(); }
    float GetClipDurationSec(int clipIndex = 0) const;

    const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
    Skeleton& GetSkeleton() { return m_Skeleton; }
    const Skeleton& GetSkeleton() const { return m_Skeleton; }
    const std::string& GetDirectory() const { return m_Directory; }

    // ---- 材質（MaterialLoader が組む。VS は SkinnedVS 固定）----
    const std::vector<std::shared_ptr<Material>>& GetMaterials() const { return m_Materials; }
    Material* GetMaterial(int index) const
    {
        return (index >= 0 && index < (int)m_Materials.size()) ? m_Materials[index].get() : nullptr;
    }

    // 時刻 → 各ボーンの global 行列（offset は掛けない）
    void SampleAnimation(float timeSec,
        std::vector<DirectX::SimpleMath::Matrix>& outGlobal,
        int clipIndex = 0) const;

    // submesh 毎: palette[bone] = boneOffsets[bone] * global[bone]
    void BuildSubmeshPalette(int submeshIndex,
        const std::vector<DirectX::SimpleMath::Matrix>& global,
        std::vector<DirectX::SimpleMath::Matrix>& outPalette) const;

private:
    void LoadAnimations(const aiScene* scene);

    std::vector<SubMesh>                    m_SubMeshes;
    std::vector<std::shared_ptr<Material>>  m_Materials;
    Skeleton                                m_Skeleton;
    std::vector<AnimationClip>              m_Animations;
    DirectX::SimpleMath::Matrix             m_GlobalInverse = DirectX::SimpleMath::Matrix::Identity;
    std::string                             m_Directory;
};