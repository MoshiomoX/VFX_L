#include "Model.h"
#include "Renderer.h"
#include "ResourceManager.h"
#include "ResourcePaths.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// ============================================================
// テクスチャパス探索
// ============================================================
std::wstring FindTexturePath(const std::string& directory, const std::string& texPath)
{
    if (fs::exists(texPath))
    {
        std::wstring wPath(texPath.begin(), texPath.end());
        return wPath;
    }

    std::string filename = fs::path(texPath).filename().string();

    std::string path1 = directory + filename;
    if (fs::exists(path1))
    {
        std::wstring wPath(path1.begin(), path1.end());
        return wPath;
    }

    std::string path2 = directory + "Tex/" + filename;
    if (fs::exists(path2))
    {
        std::wstring wPath(path2.begin(), path2.end());
        return wPath;
    }

    std::string path3 = directory + "textures/" + filename;
    if (fs::exists(path3))
    {
        std::wstring wPath(path3.begin(), path3.end());
        return wPath;
    }

    std::string path4 = directory + "Textures/" + filename;
    if (fs::exists(path4))
    {
        std::wstring wPath(path4.begin(), path4.end());
        return wPath;
    }

    std::string path5 = "Assets/" + filename;
    if (fs::exists(path5))
    {
        std::wstring wPath(path5.begin(), path5.end());
        return wPath;
    }

    std::cout << "[Warning] Texture not found: " << texPath << std::endl;
    return L"";
}

// ============================================================
// Assimp 行列 → SimpleMath 行列（転置）
// ============================================================
Matrix ConvertMatrix(const aiMatrix4x4& aiMat)
{
    return Matrix(
        aiMat.a1, aiMat.b1, aiMat.c1, aiMat.d1,
        aiMat.a2, aiMat.b2, aiMat.c2, aiMat.d2,
        aiMat.a3, aiMat.b3, aiMat.c3, aiMat.d3,
        aiMat.a4, aiMat.b4, aiMat.c4, aiMat.d4
    );
}

// ============================================================
// ノード再帰処理（頂点抽出 + 包囲ボックス集計）
// ============================================================
void ProcessNode(
    aiNode* node,
    const aiScene* scene,
    ID3D11Device* device,
    std::vector<Model::SubMesh>& subMeshes,
    const Matrix& parentTransform,
    Vector3& outMin,
    Vector3& outMax)
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

            // 包囲ボックス
            outMin.x = (std::min)(outMin.x, pos.x);
            outMin.y = (std::min)(outMin.y, pos.y);
            outMin.z = (std::min)(outMin.z, pos.z);
            outMax.x = (std::max)(outMax.x, pos.x);
            outMax.y = (std::max)(outMax.y, pos.y);
            outMax.z = (std::max)(outMax.z, pos.z);

            if (mesh->HasNormals())
            {
                Vector3 normal(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
                normal = Vector3::TransformNormal(normal, globalTransform);
                normal.Normalize();
                vertex.normal = normal;
            }

            if (mesh->mTangents)
            {
                Vector3 tangent(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z);
                tangent = Vector3::TransformNormal(tangent, globalTransform);
                tangent.Normalize();
                vertex.tangent = tangent;
            }
            else
            {
                vertex.tangent = Vector3(1, 0, 0);
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
        ProcessNode(node->mChildren[i], scene, device, subMeshes, globalTransform, outMin, outMax);
}

// ============================================================
// モデル読み込み
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

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cout << "[Error] Assimp: " << importer.GetErrorString() << std::endl;
        return false;
    }

    size_t lastSlash = filepath.find_last_of("/\\");
    m_Directory = (lastSlash != std::string::npos) ?
        filepath.substr(0, lastSlash + 1) : "";

    std::string modelName = fs::path(filepath).stem().string();

    auto defaultVS = ResourceManager::Get().LoadVS(L"Default", Res::Shd::DefaultVS);
    auto defaultPS = ResourceManager::Get().LoadPS(L"Default", Res::Shd::DefaultPS);

    // ===== マテリアル読み込み =====
    for (unsigned int i = 0; i < scene->mNumMaterials; i++)
    {
        aiMaterial* aiMat = scene->mMaterials[i];
        auto material = std::make_shared<Material>();

        material->SetVertexShader(defaultVS);
        material->SetPixelShader(defaultPS);

        // マテリアル名（多材质 index 確認用）
        aiString matName;
        aiMat->Get(AI_MATKEY_NAME, matName);
        std::cout << "[Material " << i << "] name: " << matName.C_Str() << std::endl;

        // --- Albedo（DIFFUSE）---
        bool albedoLoaded = false;
        if (aiMat->GetTextureCount(aiTextureType_DIFFUSE) > 0)
        {
            aiString texPath;
            aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);
            std::cout << "    diffuse: " << texPath.C_Str() << std::endl;

            std::wstring foundPath = FindTexturePath(m_Directory, texPath.C_Str());
            if (!foundPath.empty())
            {
                auto texture = ResourceManager::Get().LoadTexture(foundPath);
                if (texture)
                {
                    material->SetAlbedoTexture(texture);
                    albedoLoaded = true;
                }
            }
        }

        if (!albedoLoaded)
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
                        material->SetAlbedoTexture(texture);
                        albedoLoaded = true;
                        break;
                    }
                }
            }
        }

        if (!albedoLoaded)
            std::cout << "    [Material " << i << "] no albedo" << std::endl;

        // --- Normal ---
        auto loadNormal = [&](aiTextureType type) -> bool
            {
                if (aiMat->GetTextureCount(type) == 0) return false;
                aiString texPath;
                aiMat->GetTexture(type, 0, &texPath);
                std::wstring foundPath = FindTexturePath(m_Directory, texPath.C_Str());
                if (foundPath.empty()) return false;
                auto tex = ResourceManager::Get().LoadTexture(foundPath);
                if (!tex) return false;
                material->SetNormalTexture(tex);
                std::cout << "    normal: " << texPath.C_Str() << std::endl;
                return true;
            };
        if (!loadNormal(aiTextureType_NORMALS))
            loadNormal(aiTextureType_HEIGHT);

        // --- Metallic / Roughness / AO（あれば自動読み込み）---
        auto loadSlot = [&](aiTextureType type, Material::TextureSlot slot, const char* label)
            {
                if (aiMat->GetTextureCount(type) == 0) return;
                aiString tp;
                aiMat->GetTexture(type, 0, &tp);
                std::wstring fp = FindTexturePath(m_Directory, tp.C_Str());
                if (fp.empty()) return;
                auto tex = ResourceManager::Get().LoadTexture(fp);
                if (tex)
                {
                    material->SetTextureSlot(slot, tex);
                    std::cout << "    " << label << ": " << tp.C_Str() << std::endl;
                }
            };
        loadSlot(aiTextureType_METALNESS, Material::Metallic, "metallic");
        loadSlot(aiTextureType_DIFFUSE_ROUGHNESS, Material::Roughness, "roughness");
        loadSlot(aiTextureType_AMBIENT_OCCLUSION, Material::AO, "ao");

        // diffuse 色
        aiColor4D color;
        if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
            material->SetColor(Vector4(color.r, color.g, color.b, color.a));

        m_Materials.push_back(material);
    }

    // ===== ノード処理 + 包囲ボックス =====
    Vector3 boundsMin(1e9f, 1e9f, 1e9f);
    Vector3 boundsMax(-1e9f, -1e9f, -1e9f);

    ProcessNode(scene->mRootNode, scene, device, m_SubMeshes, Matrix::Identity, boundsMin, boundsMax);

    m_BoundsMin = boundsMin;
    m_BoundsMax = boundsMax;
    m_BoundsCenter = (boundsMin + boundsMax) * 0.5f;

    std::cout << "[OK] Model loaded: " << filepath << std::endl;
    std::cout << "     SubMeshes: " << m_SubMeshes.size()
        << "  Materials: " << m_Materials.size() << std::endl;
    std::cout << "     Center: " << m_BoundsCenter.x << ", "
        << m_BoundsCenter.y << ", " << m_BoundsCenter.z << std::endl;
    std::cout << "     Size: " << (boundsMax.x - boundsMin.x) << ", "
        << (boundsMax.y - boundsMin.y) << ", "
        << (boundsMax.z - boundsMin.z) << std::endl;

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