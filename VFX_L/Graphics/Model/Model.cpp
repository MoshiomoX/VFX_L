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
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <filesystem>
#include <algorithm>

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
static void ProcessNode(
    aiNode* node, const aiScene* scene, ID3D11Device* device,
    std::vector<Model::SubMesh>& subMeshes, const Matrix& parentTransform,
    Vector3& boundsMin, Vector3& boundsMax)
{
    const Matrix globalTransform = ConvertMatrix(node->mTransformation) * parentTransform;

    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

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

            vertex.color = Vector4(1, 1, 1, 1);
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
            boundsMin, boundsMax);
}

// ============================================================
// ファイルから（import して LoadFromScene へ）
// ============================================================
bool Model::Load(ID3D11Device* device, const std::string& filepath)
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

    return LoadFromScene(device, scene, dir, name);
}

// ============================================================
// import 済みの scene から組み立てる（LoadModelAuto もここに来る）
// ============================================================
bool Model::LoadFromScene(ID3D11Device* device, const aiScene* scene,
    const std::string& directory, const std::string& modelName)
{
    m_Directory = directory;

    m_Materials = MaterialLoader::LoadFromScene(device, scene, directory, modelName);

    // ---------- メッシュ + 包囲ボックス ----------
    m_BoundsMin = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
    m_BoundsMax = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    ProcessNode(scene->mRootNode, scene, device, m_SubMeshes, Matrix::Identity,
        m_BoundsMin, m_BoundsMax);
    if (m_SubMeshes.empty())
        m_BoundsMin = m_BoundsMax = Vector3::Zero;
    m_BoundsCenter = (m_BoundsMin + m_BoundsMax) * 0.5f;

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