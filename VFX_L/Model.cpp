#include "Model.h"
#include "Renderer.h"
#include "ResourceManager.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace DirectX::SimpleMath;

// テクスチャを複数の候補ディレクトリから探す
std::wstring FindTexturePath(const std::string& directory, const std::string& texPath)
{
    if (fs::exists(texPath))
    {
        std::wstring wPath(texPath.begin(), texPath.end());
        return wPath;
    }

    std::string filename = fs::path(texPath).filename().string();

    std::string path1 = directory + filename;
    if (fs::exists(path1)) { std::wstring w(path1.begin(), path1.end()); return w; }

    std::string path2 = directory + "textures/" + filename;
    if (fs::exists(path2)) { std::wstring w(path2.begin(), path2.end()); return w; }

    std::string path3 = directory + "Textures/" + filename;
    if (fs::exists(path3)) { std::wstring w(path3.begin(), path3.end()); return w; }

    std::string path4 = "Assets/" + filename;
    if (fs::exists(path4)) { std::wstring w(path4.begin(), path4.end()); return w; }

    std::cout << "[Warning] Texture not found: " << texPath << std::endl;
    return L"";
}

// Assimp行列 → SimpleMath行列（転置）
Matrix ConvertMatrix(const aiMatrix4x4& aiMat)
{
    return Matrix(
        aiMat.a1, aiMat.b1, aiMat.c1, aiMat.d1,
        aiMat.a2, aiMat.b2, aiMat.c2, aiMat.d2,
        aiMat.a3, aiMat.b3, aiMat.c3, aiMat.d3,
        aiMat.a4, aiMat.b4, aiMat.c4, aiMat.d4
    );
}

// 前方宣言
void ProcessNode(
    aiNode* node,
    const aiScene* scene,
    ID3D11Device* device,
    std::vector<Model::SubMesh>& subMeshes,
    const Matrix& parentTransform);

// ============================================
// import済みシーンから構築（LoadModelAuto から呼ばれる）
// ============================================
bool Model::LoadFromScene(ID3D11Device* device, const aiScene* scene,
    const std::string& directory, const std::string& modelName)
{
    m_Directory = directory;

    auto defaultVS = ResourceManager::Get().LoadVS(L"Default", L"Shader/VS.hlsl");
    auto defaultPS = ResourceManager::Get().LoadPS(L"Default", L"Shader/PS.hlsl");

    for (unsigned int i = 0; i < scene->mNumMaterials; i++)
    {
        aiMaterial* aiMat = scene->mMaterials[i];
        auto material = std::make_shared<Material>();

        material->SetVertexShader(defaultVS);
        material->SetPixelShader(defaultPS);

        bool textureLoaded = false;

        if (aiMat->GetTextureCount(aiTextureType_DIFFUSE) > 0)
        {
            aiString texPath;
            aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);

            std::wstring foundPath = FindTexturePath(m_Directory, texPath.C_Str());
            if (!foundPath.empty())
            {
                auto texture = ResourceManager::Get().LoadTexture(foundPath);
                if (texture)
                {
                    material->SetTexture(texture);
                    textureLoaded = true;
                    std::wcout << L"[Model] Texture loaded: " << foundPath << std::endl;
                }
            }
        }

        if (!textureLoaded)
        {
            std::vector<std::string> exts = { ".png", ".jpg", ".jpeg", ".tga", ".dds" };
            for (const auto& ext : exts)
            {
                std::string texPath = m_Directory + modelName + ext;
                if (fs::exists(texPath))
                {
                    std::wstring wPath(texPath.begin(), texPath.end());
                    auto texture = ResourceManager::Get().LoadTexture(wPath);
                    if (texture)
                    {
                        material->SetTexture(texture);
                        textureLoaded = true;
                        break;
                    }
                }
            }
        }

        if (!textureLoaded)
            std::cout << "[Model] Material " << i << " has no texture" << std::endl;

        aiColor4D color;
        if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
            material->SetColor(Vector4(color.r, color.g, color.b, color.a));

        m_Materials.push_back(material);
    }

    ProcessNode(scene->mRootNode, scene, device, m_SubMeshes, Matrix::Identity);

    std::cout << "[OK] Model loaded (static)" << std::endl;
    std::cout << "     SubMeshes: " << m_SubMeshes.size() << std::endl;
    std::cout << "     Materials: " << m_Materials.size() << std::endl;
    return true;
}

// ============================================
// 既存Load：単体で呼ぶ用（importしてLoadFromSceneへ委譲）
// ============================================
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

    size_t lastSlash = filepath.find_last_of("/\\");
    std::string dir = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash + 1) : "";
    std::string name = fs::path(filepath).stem().string();

    return LoadFromScene(device, scene, dir, name);
}

// ============================================
// ノード走査（静的：変換を頂点に焼き込む）
// ============================================
void ProcessNode(
    aiNode* node,
    const aiScene* scene,
    ID3D11Device* device,
    std::vector<Model::SubMesh>& subMeshes,
    const Matrix& parentTransform)
{
    Matrix nodeTransform = ConvertMatrix(node->mTransformation);
    Matrix globalTransform = nodeTransform * parentTransform;

    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

        std::vector<VERTEX_3D> vertices;
        std::vector<unsigned int> indices;

        for (unsigned int v = 0; v < mesh->mNumVertices; v++)
        {
            VERTEX_3D vertex = {};

            Vector3 pos(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            pos = Vector3::Transform(pos, globalTransform);
            vertex.position = pos;

            if (mesh->HasNormals())
            {
                Vector3 normal(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
                normal = Vector3::TransformNormal(normal, globalTransform);
                normal.Normalize();
                vertex.normal = normal;
            }

            // ★接線（tangent）を読む。CalcTangentSpace で生成済み
            if (mesh->HasTangentsAndBitangents())
            {
                Vector3 tangent(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z);
                tangent = Vector3::TransformNormal(tangent, globalTransform);
                tangent.Normalize();
                vertex.tangent = tangent;
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
            aiFace& face = mesh->mFaces[f];
            for (unsigned int idx = 0; idx < face.mNumIndices; idx++)
                indices.push_back(face.mIndices[idx]);
        }

        auto meshPtr = std::make_shared<Mesh>();
        if (!meshPtr->Create(device, vertices, indices))
        {
            std::cout << "[Error] Failed to create mesh" << std::endl;
            continue;
        }

        Model::SubMesh subMesh;
        subMesh.mesh = meshPtr;
        subMesh.materialIndex = mesh->mMaterialIndex;
        subMeshes.push_back(subMesh);
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++)
        ProcessNode(node->mChildren[i], scene, device, subMeshes, globalTransform);
}
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