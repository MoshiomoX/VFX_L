// Skeleton.h
#pragma once
#include <SimpleMath.h>
#include <string>
#include <vector>
#include <unordered_map>

// ============================================
// ボーン1本分の静的データ（読み込み時に確定、以後不変）
// ============================================
struct Bone
{
    std::string name;                  // ボーン名（aiBone / aiNode と対応）
    int         parentIndex = -1;      // 親ボーンのindex（ROOTは -1）

    // メッシュ空間 → ボーン空間 への変換（= bind poseの逆行列, assimpのmOffsetMatrix）
    DirectX::SimpleMath::Matrix offsetMatrix = DirectX::SimpleMath::Matrix::Identity;

    // 親基準のローカル変換（アニメ無し=bind時のnodeTransform）
    DirectX::SimpleMath::Matrix localBindTransform = DirectX::SimpleMath::Matrix::Identity;
};

// ============================================
// スケルトン（ボーン階層まるごと）
// index順の配列 + 名前引きmap
// ============================================
class Skeleton
{
public:
    int GetBoneCount() const { return static_cast<int>(m_Bones.size()); }
    std::vector<Bone>& GetBones() { return m_Bones; }
    const std::vector<Bone>& GetBones() const { return m_Bones; }
    Bone& GetBone(int index) { return m_Bones[index]; }

    // 名前 → index（無ければ -1）
    int FindBoneIndex(const std::string& name) const
    {
        auto it = m_NameToIndex.find(name);
        return (it != m_NameToIndex.end()) ? it->second : -1;
    }

    // ボーン追加してindexを返す（既にあればそのindex）
    int AddBone(const std::string& name)
    {
        if (auto it = m_NameToIndex.find(name); it != m_NameToIndex.end())
            return it->second;

        int index = static_cast<int>(m_Bones.size());
        Bone b;
        b.name = name;
        m_Bones.push_back(b);
        m_NameToIndex[name] = index;
        return index;
    }

private:
    std::vector<Bone>                    m_Bones;       // index順
    std::unordered_map<std::string, int> m_NameToIndex; // 名前→index
}; 
