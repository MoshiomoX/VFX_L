#pragma once
#include <vector>
#include <memory>
#include <string>
#include <d3d11.h>
#include <SimpleMath.h>
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Material/Material.h"
#include "Graphics/Transform.h"

using namespace DirectX::SimpleMath;

class Renderer;
struct aiScene; 
class Model
{
public:
    struct SubMesh
    {
        std::shared_ptr<Mesh> mesh;
        int materialIndex = -1;
    };

  //  bool Load(ID3D11Device* device, const std::string& filepath);
    void Draw(Renderer& renderer, Transform* transform);

    size_t GetSubMeshCount() const { return m_SubMeshes.size(); }
    size_t GetMaterialCount() const { return m_Materials.size(); }

    void SetMaterial(int index, std::shared_ptr<Material> material);
    Material* GetMaterial(int index) const;

    // 包囲ボックス
    Vector3 GetBoundsMin() const { return m_BoundsMin; }
    Vector3 GetBoundsMax() const { return m_BoundsMax; }
    Vector3 GetBoundsCenter() const { return m_BoundsCenter; }

    // ファイルの 1 単位が何 m か（FBX の UnitScaleFactor / 100。cm で書かれた FBX は 0.01、
    // 情報が無い形式は 1）。頂点・包囲ボックスには掛けていない
    // （既存のモデルは使う側がそれぞれ手で倍率を持っているので、読み込みでは変えない）。
    // ステージ編集のように素材の実寸で並べたい所が掛ける
    float GetFileUnitScale() const { return m_FileUnitScale; }

    const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
    bool Load(ID3D11Device* device, const std::string& filepath);

    // 読み込みの追加指定（Load の 3 引数版）。雑魚のように「1 体分を焼いて使い回す」用
    struct LoadOptions
    {
        // 全頂点に最後に掛ける（拡縮・向き・足元合わせ）。包囲ボックスも掛けた後の値になる
        Matrix rootTransform = Matrix::Identity;
        // 節点アニメ（骨を持たず、部品を節点で動かす FBX）の 1 フレームを姿勢として焼く。
        // 空なら焼かない（バインドポーズのまま）。名前は "Rig|walk" の末尾一致でもよい
        std::string poseClip;
        float poseTimeFrac = 0.0f;   // 0..1（クリップの長さに対する割合）
    };
    bool Load(ID3D11Device* device, const std::string& filepath, const LoadOptions& opt);

    // 節点アニメ（部品を節点で動かす FBX）の標本取り。
    // timeFracs の各時刻（0..1）で、submesh 毎の全体変換（節点 × rootTransform）を返す。
    // 並びは Load で作られる submesh と同じ（ProcessNode の順）。
    // 雑魚の部品アニメ（GPU で 1 部品 1 行列）の表を作るのに使う
    static bool SampleSubmeshTransforms(const std::string& filepath, const std::string& clip,
        const std::vector<float>& timeFracs, const Matrix& rootTransform,
        std::vector<std::vector<Matrix>>& out, float* outDurationSec = nullptr);

    bool LoadFromScene(ID3D11Device* device, const aiScene* scene,
        const std::string& directory, const std::string& modelName,
        const Matrix& rootTransform = Matrix::Identity);
    // プログラム生成メッシュを追加する（PrimitiveBuilder 用）
    void AddSubMesh(std::shared_ptr<Mesh> mesh, int materialIndex = -1)
    {
        SubMesh sub;
        sub.mesh = mesh;
        sub.materialIndex = materialIndex;
        m_SubMeshes.push_back(sub);
    }

    // マテリアルを直接追加
    void AddMaterial(std::shared_ptr<Material> mat)
    {
        m_Materials.push_back(mat);
    }
private:
    std::vector<SubMesh> m_SubMeshes;
    std::vector<std::shared_ptr<Material>> m_Materials;
    std::string m_Directory;

    Vector3 m_BoundsMin = { 0, 0, 0 };
    Vector3 m_BoundsMax = { 0, 0, 0 };
    Vector3 m_BoundsCenter = { 0, 0, 0 };

    float m_FileUnitScale = 1.0f;   // Load() が FBX の UnitScaleFactor から決める
};