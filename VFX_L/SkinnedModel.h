#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include "SkinnedVertex.h"
#include "Skeleton.h"
// アニメのキーフレーム
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
struct aiScene; 
class SkinnedModel
{
public:
    // 1 submesh分のCPUデータ（bind pose）
    struct SubMesh
    {
        std::string                name;
        std::vector<SkinnedVertex> vertices; // bind pose（mesh空間、変換は焼かない）
        std::vector<uint32_t>      indices;
        int                        materialIndex = -1;
    };

    bool Load(const std::string& filepath);                
    bool LoadFromScene(const aiScene* scene, const std::string& directory);

    bool  HasAnimation() const { return !m_Animations.empty(); }
    void  SampleAnimation(float timeSec, std::vector<DirectX::SimpleMath::Matrix>& outPalette,
        int clipIndex = 0) const;     // 時刻→ボーン最終行列
    float GetClipDurationSec(int clipIndex = 0) const;


    const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
    Skeleton& GetSkeleton() { return m_Skeleton; }
    const std::string& GetDirectory() const { return m_Directory; }
    const Skeleton& GetSkeleton() const { return m_Skeleton; }   // ← 追加


private:
    std::vector<AnimationClip>      m_Animations;
    DirectX::SimpleMath::Matrix     m_GlobalInverse = DirectX::SimpleMath::Matrix::Identity;
    void  LoadAnimations(const aiScene* scene);

    std::vector<SubMesh> m_SubMeshes;
    Skeleton             m_Skeleton;
    std::string          m_Directory;
};