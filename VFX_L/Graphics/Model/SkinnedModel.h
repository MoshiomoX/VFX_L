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

// 骨 1 本の親基準の姿勢（ブレンドはこの形で行う。行列にしてから混ぜると歪む）
struct BoneLocal
{
    DirectX::SimpleMath::Vector3    scale = { 1, 1, 1 };
    DirectX::SimpleMath::Quaternion rotation;   // 単位
    DirectX::SimpleMath::Vector3    translation;
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
        std::string                name;       // mesh 名（Blender だと "Cube.153" のような機械名）
        std::string                nodeName;   // mesh を持つノード名（"Mage_Hat" / "2H_Staff"。表示切替はこちらで引く）
        std::vector<SkinnedVertex> vertices;   // bind pose（mesh 空間、変換は焼かない）
        std::vector<uint32_t>      indices;
        int                        materialIndex = -1;

        // submesh 毎の offset 行列（同じ骨でも submesh によって違い得る）
        std::unordered_map<int, DirectX::SimpleMath::Matrix> boneOffsets;
        // mesh を持つノードの bind の global。スキニングした時の骨の global = Invert(offset) * nodeGlobal
        DirectX::SimpleMath::Matrix nodeGlobal = DirectX::SimpleMath::Matrix::Identity;
    };

    bool Load(ID3D11Device* device, const std::string& filepath);
    bool LoadFromScene(ID3D11Device* device, const aiScene* scene,
        const std::string& directory, const std::string& modelName);

    bool  HasAnimation() const { return !m_Animations.empty(); }
    int   GetClipCount() const { return (int)m_Animations.size(); }
    float GetClipDurationSec(int clipIndex = 0) const;
    // クリップ名 → index。見つからなければ -1。
    // Blender 出力の FBX は "Rig|Idle" のように接頭辞が付くので、末尾一致も許す
    int   FindClip(const std::string& name) const;
    const std::string& GetClipName(int clipIndex) const { return m_Animations[clipIndex].name; }

    // 別ファイルのアニメを骨名で足す（同じ骨名の別の人形用に作られたクリップ。
    // 例: KayKit Character Animations の Rig_Medium_*.fbx → Adventurers 1.0 の Mage）。
    //   回転   : 向こうの bind からの差分（親空間）を自分の bind に掛ける  R = R_bind * inv(A_bind) * A
    //   平行移動: 差分を hips の bind の長さの比で縮めて自分の bind に足す（骨の長さは自分のまま）
    //   拡縮   : 向こうの bind に対する比を掛ける
    // 両方の bind が同じなら元のキーそのもの。自分に無い骨のチャンネルは捨てる。
    // 同じ名前のクリップが既にあれば、足す方を「ファイル名|名前」にする
    // （"Rig_Medium_MovementAdvanced|Dodge_Forward"。FindClip("Dodge_Forward") は元の方）。
    // 戻り値 = 足したクリップ数（読めなければ -1）
    //
    // map を渡すと（2026-10-04）骨名の違う人形（例: CC_Base_* ← UE 風）から世界空間で付け替える：
    //   自分の基準 = スキニングした時の姿勢（offset から逆算。ノードの既定の姿勢は飾りのポーズの事がある）。
    //   基準どうしの姿勢の違い（T ポーズ ↔ A ポーズ）は、対応する骨の向き（骨 → 子の骨）が向こうの
    //   基準に揃うよう骨ごとに最小の回転で直す。そのうえで 30fps で向こうの骨の世界の回転の
    //   基準からの差分を両モデルの向きの差で回して掛け、親で割って親基準に戻して焼く。
    //   腰（map の先頭）は平行移動も腰の高さの比で縮めて付ける。対応の無い骨（マント、捻り骨）は
    //   スキニングした時の親基準の姿勢のまま親に付いて行く
    int AddAnimationsFromFile(const std::string& filepath,
        const struct BoneMapEntry* map = nullptr, int mapCount = 0);

    const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }

    // スキニングした時の骨の global（モデル空間）= Invert(offset) * mesh ノード。
    // offset の無い骨（末端の _end 等）は親の値にノードの親基準の変換を掛ける
    DirectX::SimpleMath::Matrix GetSkinBindGlobal(int boneIndex) const;
    // ノード名 → submesh index（無ければ -1）
    int FindSubMeshByNode(const std::string& nodeName) const;
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
    // 1 クリップをそのまま流す簡易版。ブレンドするなら下の SampleLocal 系を使う
    void SampleAnimation(float timeSec,
        std::vector<DirectX::SimpleMath::Matrix>& outGlobal,
        int clipIndex = 0) const;

    // ---- ブレンド用の分解版 ----
    // 時刻 → 各ボーンの親基準 S/R/T（チャンネルの無い骨は bind）
    void SampleLocal(int clipIndex, float timeSec, std::vector<BoneLocal>& outLocal) const;
    // 親基準 S/R/T → global 行列
    void BuildGlobals(const std::vector<BoneLocal>& local,
        std::vector<DirectX::SimpleMath::Matrix>& outGlobal) const;
    // a と b を t で混ぜて a に書く（t=0 → a のまま、t=1 → b）。
    // weights を渡すと骨毎に t を掛ける（上半身だけ差し替える等）
    static void BlendLocals(std::vector<BoneLocal>& a, const std::vector<BoneLocal>& b,
        float t, const std::vector<float>* weights = nullptr);
    // rootBone とその子孫を 1、他を 0 にした骨マスクを作る（無い名前なら false）
    bool BuildBoneMask(const std::string& rootBone, std::vector<float>& outMask) const;

    // ---- 1 フレームを CPU でスキニングして静的 Model にする ----
    // 雑魚のようにインスタンス描画したい相手用（骨は持たせない）。
    //   xform      … スキニング後の頂点に掛ける（拡縮・向き・足元合わせ）
    //   skipNodes  … 含めない submesh のノード名（持たせない武器など）
    // 材質は付けない（呼び側が VS/PS/テクスチャを持つ）。頂点色は白
    std::shared_ptr<class Model> BakeStatic(ID3D11Device* device, int clipIndex, float timeSec,
        const DirectX::SimpleMath::Matrix& xform,
        const std::vector<std::string>& skipNodes = {}) const;

    // submesh 毎: palette[bone] = boneOffsets[bone] * global[bone]
    void BuildSubmeshPalette(int submeshIndex,
        const std::vector<DirectX::SimpleMath::Matrix>& global,
        std::vector<DirectX::SimpleMath::Matrix>& outPalette) const;

private:
    void LoadAnimations(const aiScene* scene);
    // AddAnimationsFromFile の骨名の対応表あり版（世界空間で付け替えて焼く）
    int RetargetAnimations(const aiScene* scene, const std::string& filepath,
        const struct BoneMapEntry* map, int mapCount);

    std::vector<SubMesh>                    m_SubMeshes;
    std::vector<std::shared_ptr<Material>>  m_Materials;
    Skeleton                                m_Skeleton;
    std::vector<AnimationClip>              m_Animations;
    DirectX::SimpleMath::Matrix             m_GlobalInverse = DirectX::SimpleMath::Matrix::Identity;
    std::string                             m_Directory;
};