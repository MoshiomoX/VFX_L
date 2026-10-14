// ============================================================
// Model.cpp
// 静的モデルの読み込み（assimp）
//   - マテリアル: 各スロットを候補順に探し、埋め込み / 外部の両方に対応
//   - シェーダー: 法線 or 金属/粗さ があれば PBR、無ければ Lambert
//   - メッシュ: ノード変換を頂点に焼き込んで SubMesh にする
// ============================================================
#include "Graphics/Model/Model.h"
#include "Graphics/Renderer/Renderer.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Model/MaterialLoader.h"
#include "Collider/CollisionMath.h"   // DopDirections（衝突用の凸包の点）

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace fs = std::filesystem;
using namespace DirectX::SimpleMath;

// ============================================================
// assimp の行列 → SimpleMath（転置）
// ============================================================
static Matrix ConvertMatrix(const aiMatrix4x4& m)
{
    return Matrix(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4);
}

// ============================================================
// ノードを再帰で辿り、変換を頂点に焼き込んで SubMesh を作る
// ============================================================
// 衝突用の凸包の点：CollisionMath::DopDirections の各方向で一番外にある頂点を集める
struct HullAccum
{
    static constexpr int kDirs = CollisionMath::kDopDirs;
    const Vector3* dir = CollisionMath::DopDirections();
    float   best[kDirs];
    Vector3 point[kDirs];
    HullAccum()
    {
        for (int i = 0; i < kDirs; ++i) { best[i] = -FLT_MAX; point[i] = Vector3::Zero; }
    }
    void Add(const Vector3& p)
    {
        for (int i = 0; i < kDirs; ++i)
        {
            const float v = dir[i].Dot(p);
            if (v > best[i]) { best[i] = v; point[i] = p; }
        }
    }
    std::vector<Vector3> Points() const   // 同じ点は 1 つに
    {
        std::vector<Vector3> out;
        for (int i = 0; i < kDirs; ++i)
        {
            if (best[i] == -FLT_MAX) continue;
            bool dup = false;
            for (const auto& q : out) if ((q - point[i]).LengthSquared() < 1e-10f) { dup = true; break; }
            if (!dup) out.push_back(point[i]);
        }
        return out;
    }
};

static void ProcessNode(
    aiNode* node, const aiScene* scene, ID3D11Device* device,
    std::vector<Model::SubMesh>& subMeshes, const Matrix& parentTransform,
    Vector3& boundsMin, Vector3& boundsMax,
    const std::vector<std::shared_ptr<Material>>& materials, HullAccum& hull)
{
    const Matrix globalTransform = ConvertMatrix(node->mTransformation) * parentTransform;

    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

        // 色のテクスチャが無い材質（色だけで塗った低ポリ素材。Quaternius・dglopez など）は、
        // 材質の拡散色を頂点色に焼く。PS は テクスチャ × 頂点色 なので、焼かないと白くなる
        // （Material::m_Color は描画で使っていない）。テクスチャのある材質は触らない
        // （Blender の FBX は拡散色 0.8 を書くことが多く、掛けると今の素材が暗くなる）
        Vector4 baseColor(1, 1, 1, 1);
        if (mesh->mMaterialIndex < materials.size() && materials[mesh->mMaterialIndex]
            && !materials[mesh->mMaterialIndex]->HasTexture()
            && mesh->mMaterialIndex < scene->mNumMaterials)
        {
            const aiMaterial* m = scene->mMaterials[mesh->mMaterialIndex];
            aiColor4D c;
            if (m->Get(AI_MATKEY_BASE_COLOR, c) == AI_SUCCESS || m->Get(AI_MATKEY_COLOR_DIFFUSE, c) == AI_SUCCESS)
                baseColor = Vector4(c.r, c.g, c.b, 1.0f);
        }

        std::vector<VERTEX_3D> vertices;
        std::vector<unsigned int> indices;
        vertices.reserve(mesh->mNumVertices);

        for (unsigned int v = 0; v < mesh->mNumVertices; v++)
        {
            VERTEX_3D vertex = {};

            Vector3 pos(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            pos = Vector3::Transform(pos, globalTransform);
            vertex.position = pos;

            boundsMin = Vector3::Min(boundsMin, pos);
            boundsMax = Vector3::Max(boundsMax, pos);
            hull.Add(pos);

            if (mesh->HasNormals())
            {
                Vector3 n(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
                n = Vector3::TransformNormal(n, globalTransform);
                n.Normalize();
                vertex.normal = n;
            }

            // 接線は CalcTangentSpace で生成済みの想定
            if (mesh->HasTangentsAndBitangents())
            {
                Vector3 t(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z);
                t = Vector3::TransformNormal(t, globalTransform);
                t.Normalize();
                vertex.tangent = t;
            }

            if (mesh->mTextureCoords[0])
            {
                vertex.uv.x = mesh->mTextureCoords[0][v].x;
                vertex.uv.y = mesh->mTextureCoords[0][v].y;
            }

            vertex.color = baseColor;
            vertices.push_back(vertex);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; f++)
        {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int idx = 0; idx < face.mNumIndices; idx++)
                indices.push_back(face.mIndices[idx]);
        }

        auto meshPtr = std::make_shared<Mesh>();
        if (!meshPtr->Create(device, vertices, indices))
        {
            std::cout << "[Error] Failed to create mesh" << std::endl;
            continue;
        }

        Model::SubMesh sub;
        sub.mesh = meshPtr;
        sub.materialIndex = (int)mesh->mMaterialIndex;
        subMeshes.push_back(sub);
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++)
        ProcessNode(node->mChildren[i], scene, device, subMeshes, globalTransform,
            boundsMin, boundsMax, materials, hull);
}

// ============================================================
// 名前でアニメを引く（"Rig|walk" のような接頭辞付きは末尾一致）
// ============================================================
static const aiAnimation* FindAnimation(const aiScene* scene, const std::string& clip)
{
    const std::string suffix = "|" + clip;
    for (unsigned int i = 0; i < scene->mNumAnimations; ++i)
    {
        const std::string n = scene->mAnimations[i]->mName.C_Str();
        if (n == clip || (n.size() > suffix.size()
            && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0))
            return scene->mAnimations[i];
    }
    return nullptr;
}

// ============================================================
// ノードアニメの 1 時刻を各ノードの mTransformation へ書き込む。
// 骨（skin weights）を持たず、部品をノードで動かす FBX（Kenney Blocky 等）の
// 姿勢を焼く・部品アニメを表にするため。前後のキーを補間する
// （位置・拡縮は線形、回転は球面線形）
// ============================================================
static bool ApplyNodePose(aiScene* scene, const std::string& clip, float timeFrac)
{
    const aiAnimation* anim = FindAnimation(scene, clip);
    if (!anim) return false;

    const double t = anim->mDuration * (double)std::clamp(timeFrac, 0.0f, 1.0f);

    // t を挟む 2 つのキーと、その間の割合
    auto bracket = [t](const auto* keys, unsigned int count, unsigned int& k0, unsigned int& k1) -> float
        {
            k0 = 0;
            while (k0 + 1 < count && keys[k0 + 1].mTime <= t) ++k0;
            k1 = (k0 + 1 < count) ? k0 + 1 : k0;
            const double span = keys[k1].mTime - keys[k0].mTime;
            return (span > 1e-9) ? (float)std::clamp((t - keys[k0].mTime) / span, 0.0, 1.0) : 0.0f;
        };

    for (unsigned int c = 0; c < anim->mNumChannels; ++c)
    {
        const aiNodeAnim* ch = anim->mChannels[c];
        aiNode* node = scene->mRootNode->FindNode(ch->mNodeName);
        if (!node) continue;

        // キーの無い成分は今の値のまま
        aiVector3D s(1, 1, 1), p(0, 0, 0);
        aiQuaternion r;
        node->mTransformation.Decompose(s, r, p);
        unsigned int a = 0, b = 0;
        if (ch->mNumScalingKeys)
        {
            const float w = bracket(ch->mScalingKeys, ch->mNumScalingKeys, a, b);
            s = ch->mScalingKeys[a].mValue + (ch->mScalingKeys[b].mValue - ch->mScalingKeys[a].mValue) * w;
        }
        if (ch->mNumRotationKeys)
        {
            const aiQuatKey* keys = ch->mRotationKeys;
            const unsigned int n = ch->mNumRotationKeys;
            float w = bracket(keys, n, a, b);
            // assimp が FBX のオイラー角の曲線から作る回転キーには、角度が 0 を跨ぐ所（時刻が整数でない所）に
            // 1 個だけ 180 度近く裏返ったキーが挟まることがある（2026-10-03。Kenney Blocky の attack-melee-right で
            // torso / head の t=12.523 など。攻撃の真ん中で上半身が地面の下へ回っていた）。
            // 前後どちらのキーとも 120 度以上離れ、前後同士は近いキーは無かった事にして、前後で補間する
            auto absDot = [](const aiQuaternion& p0, const aiQuaternion& p1)
                {
                    return std::fabs(p0.w * p1.w + p0.x * p1.x + p0.y * p1.y + p0.z * p1.z);
                };
            auto isSpike = [&](unsigned int i)
                {
                    if (i == 0 || i + 1 >= n) return false;
                    return absDot(keys[i - 1].mValue, keys[i].mValue) < 0.5f
                        && absDot(keys[i].mValue, keys[i + 1].mValue) < 0.5f
                        && absDot(keys[i - 1].mValue, keys[i + 1].mValue) > 0.7f;
                };
            bool skipped = false;
            while (a > 0 && isSpike(a)) { --a; skipped = true; }
            while (b + 1 < n && isSpike(b)) { ++b; skipped = true; }
            if (skipped)
            {
                const double span = keys[b].mTime - keys[a].mTime;
                w = (span > 1e-9) ? (float)std::clamp((t - keys[a].mTime) / span, 0.0, 1.0) : 0.0f;
            }
            aiQuaternion::Interpolate(r, keys[a].mValue, keys[b].mValue, w);
            r.Normalize();
        }
        if (ch->mNumPositionKeys)
        {
            const float w = bracket(ch->mPositionKeys, ch->mNumPositionKeys, a, b);
            p = ch->mPositionKeys[a].mValue + (ch->mPositionKeys[b].mValue - ch->mPositionKeys[a].mValue) * w;
        }
        node->mTransformation = aiMatrix4x4(s, r, p);
    }
    return true;
}

// ProcessNode と同じ順（ノードの mesh → 子）で、submesh 毎の全体変換を集める
static void CollectMeshGlobals(const aiNode* node, const Matrix& parentTransform, std::vector<Matrix>& out)
{
    const Matrix globalTransform = ConvertMatrix(node->mTransformation) * parentTransform;
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
        out.push_back(globalTransform);
    for (unsigned int i = 0; i < node->mNumChildren; i++)
        CollectMeshGlobals(node->mChildren[i], globalTransform, out);
}

// ============================================================
// 部品アニメの表を作るためのサンプリングり（Model.h の説明を参照）
// ============================================================
bool Model::SampleSubmeshTransforms(const std::string& filepath, const std::string& clip,
    const std::vector<float>& timeFracs, const Matrix& rootTransform,
    std::vector<std::vector<Matrix>>& out, float* outDurationSec)
{
    out.clear();
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals |
        aiProcess_MakeLeftHanded);   // Load と同じ前処理（ノードの並びと座標系を揃える）
    if (!scene || !scene->mRootNode) return false;

    const aiAnimation* anim = FindAnimation(scene, clip);
    if (!anim) return false;
    if (outDurationSec)
    {
        const double tps = (anim->mTicksPerSecond > 0.0) ? anim->mTicksPerSecond : 25.0;
        *outDurationSec = (float)(anim->mDuration / tps);
    }

    aiScene* editable = const_cast<aiScene*>(scene);   // importer が持つ scene。この関数の中だけで書き換える
    for (float f : timeFracs)
    {
        ApplyNodePose(editable, clip, f);
        std::vector<Matrix> globals;
        CollectMeshGlobals(scene->mRootNode, rootTransform, globals);
        out.push_back(std::move(globals));
    }
    return true;
}

// ============================================================
// ファイルから（import して LoadFromScene へ）
// ============================================================
bool Model::Load(ID3D11Device* device, const std::string& filepath)
{
    return Load(device, filepath, LoadOptions());
}

bool Model::Load(ID3D11Device* device, const std::string& filepath, const LoadOptions& opt)
{
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals |
        aiProcess_MakeLeftHanded);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        std::cout << "[Error] Assimp: " << importer.GetErrorString() << std::endl;
        return false;
    }

    const size_t lastSlash = filepath.find_last_of("/\\");
    const std::string dir = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash + 1) : "";
    const std::string name = fs::path(filepath).stem().string();

    // FBX の単位（UnitScaleFactor = 1 単位が何 cm か）。Assimp は頂点に掛けないので控えるだけ。
    // Assimp の版によって double / float どちらで入るかが違うので両方試す
    m_FileUnitScale = 1.0f;
    if (scene->mMetaData)
    {
        double d = 0.0;
        float f = 0.0f;
        if (scene->mMetaData->Get("UnitScaleFactor", d) && d > 0.0)
            m_FileUnitScale = (float)(d * 0.01);
        else if (scene->mMetaData->Get("UnitScaleFactor", f) && f > 0.0f)
            m_FileUnitScale = f * 0.01f;
    }

    // ノードアニメの姿勢を焼く（importer が持つ scene を書き換える。この関数の中だけで使うので問題ない）
    if (!opt.poseClip.empty()
        && !ApplyNodePose(const_cast<aiScene*>(scene), opt.poseClip, opt.poseTimeFrac))
    {
        std::cout << "[Warning] Model: pose clip not found: " << opt.poseClip
            << " in " << filepath << " (bind pose)" << std::endl;
    }

    return LoadFromScene(device, scene, dir, name, opt.rootTransform);
}

// ============================================================
// import 済みの scene から組み立てる（LoadModelAuto もここに来る）
// ============================================================
bool Model::LoadFromScene(ID3D11Device* device, const aiScene* scene,
    const std::string& directory, const std::string& modelName,
    const Matrix& rootTransform)
{
    m_Directory = directory;

    m_Materials = MaterialLoader::LoadFromScene(device, scene, directory, modelName);

    // ---------- メッシュ + 包囲ボックス ----------
    m_BoundsMin = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
    m_BoundsMax = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    // rootTransform は全ノードの一番外側に掛かる（行ベクトル: v * ノード * root）
    HullAccum hull;
    ProcessNode(scene->mRootNode, scene, device, m_SubMeshes, rootTransform,
        m_BoundsMin, m_BoundsMax, m_Materials, hull);
    if (m_SubMeshes.empty())
        m_BoundsMin = m_BoundsMax = Vector3::Zero;
    m_BoundsCenter = (m_BoundsMin + m_BoundsMax) * 0.5f;
    m_HullPoints = hull.Points();

    std::cout << "[OK] Model loaded (static)  SubMeshes: " << m_SubMeshes.size()
        << "  Materials: " << m_Materials.size() << std::endl;
    return true;
}

// ============================================================
// 描画
// ============================================================
void Model::Draw(Renderer& renderer, Transform* transform)
{
    for (auto& sub : m_SubMeshes)
    {
        Material* mat = nullptr;
        if (sub.materialIndex >= 0 && sub.materialIndex < (int)m_Materials.size())
            mat = m_Materials[sub.materialIndex].get();
        renderer.DrawMesh(sub.mesh.get(), transform, mat);
    }
}

void Model::SetMaterial(int index, std::shared_ptr<Material> material)
{
    if (index >= 0 && index < (int)m_Materials.size())
        m_Materials[index] = material;
}

Material* Model::GetMaterial(int index) const
{
    if (index >= 0 && index < (int)m_Materials.size())
        return m_Materials[index].get();
    return nullptr;
}