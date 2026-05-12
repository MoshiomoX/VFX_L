#include "Model.h"
#include "Renderer.h"
#include "ResourceManager.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

// 工具函数：尝试多种路径找到纹理
std::wstring FindTexturePath(const std::string& directory, const std::string& texPath)
{
    // 1. 原始路径（可能是绝对路径）
    if (fs::exists(texPath))
    {
        std::wstring wPath(texPath.begin(), texPath.end());
        return wPath;
    }

    // 2. 从路径中提取文件名
    std::string filename = fs::path(texPath).filename().string();

    // 3. 尝试：目录 + 文件名
    std::string path1 = directory + filename;
    if (fs::exists(path1))
    {
        std::wstring wPath(path1.begin(), path1.end());
        return wPath;
    }

    // 4. 尝试：目录 + textures/ + 文件名
    std::string path2 = directory + "textures/" + filename;
    if (fs::exists(path2))
    {
        std::wstring wPath(path2.begin(), path2.end());
        return wPath;
    }

    // 5. 尝试：目录 + Textures/ + 文件名
    std::string path3 = directory + "Textures/" + filename;
    if (fs::exists(path3))
    {
        std::wstring wPath(path3.begin(), path3.end());
        return wPath;
    }

    // 6. 尝试：Assets/ + 文件名
    std::string path4 = "Assets/" + filename;
    if (fs::exists(path4))
    {
        std::wstring wPath(path4.begin(), path4.end());
        return wPath;
    }

    // 找不到
    std::cout << "[Warning] Texture not found: " << texPath << std::endl;
    std::cout << "  Tried: " << path1 << std::endl;
    std::cout << "  Tried: " << path2 << std::endl;
    std::cout << "  Tried: " << path4 << std::endl;

    return L"";
}

// aiMatrix4x4 → DirectX Matrix
Matrix ConvertMatrix(const aiMatrix4x4& aiMat)
{
    return Matrix(
        aiMat.a1, aiMat.b1, aiMat.c1, aiMat.d1,
        aiMat.a2, aiMat.b2, aiMat.c2, aiMat.d2,
        aiMat.a3, aiMat.b3, aiMat.c3, aiMat.d3,
        aiMat.a4, aiMat.b4, aiMat.c4, aiMat.d4
    );
}

void ProcessNode(
    aiNode* node,
    const aiScene* scene,
    ID3D11Device* device,
    std::vector<Model::SubMesh>& subMeshes,
    const Matrix& parentTransform);
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

    // ディレクトリ取得
    size_t lastSlash = filepath.find_last_of("/\\");
    m_Directory = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash + 1) : "";

    // モデル名取得（同名テクスチャ用）
    std::string modelName = fs::path(filepath).stem().string();

    // デフォルトShader
    auto defaultShader = ResourceManager::Get().LoadShader(
        L"Default",
        L"Shader/VS.hlsl",
        L"Shader/PS.hlsl"
    );

    // マテリアル読み込み
    for (unsigned int i = 0; i < scene->mNumMaterials; i++)
    {
        aiMaterial* aiMat = scene->mMaterials[i];
        auto material = std::make_shared<Material>();
        material->SetShader(defaultShader);

        bool textureLoaded = false;

        // 1. FBXから読み取り
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

        // 2. 同名テクスチャ
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
                        std::cout << "[Model] Same-name texture loaded: " << texPath << std::endl;
                        break;
                    }
                }
            }
        }

        if (!textureLoaded)
        {
            std::cout << "[Model] Material " << i << " has no texture" << std::endl;
        }

        // 色
        aiColor4D color;
        if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
        {
            material->SetColor(Vector4(color.r, color.g, color.b, color.a));
        }

        m_Materials.push_back(material);
    }

    // ノード処理
    ProcessNode(scene->mRootNode, scene, device, m_SubMeshes, Matrix::Identity);

    std::cout << "[OK] Model loaded: " << filepath << std::endl;
    std::cout << "     SubMeshes: " << m_SubMeshes.size() << std::endl;
    std::cout << "     Materials: " << m_Materials.size() << std::endl;

    return true;
}

void ProcessNode(
    aiNode* node,
    const aiScene* scene,
    ID3D11Device* device,
    std::vector<Model::SubMesh>& subMeshes,
    const Matrix& parentTransform)
{
    // このノードの変換行列
    Matrix nodeTransform = ConvertMatrix(node->mTransformation);
    Matrix globalTransform = nodeTransform * parentTransform;

    // このノードのメッシュを処理
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

        std::vector<VERTEX_3D> vertices;
        std::vector<unsigned int> indices;

        // 頂点（変換行列を適用）
        for (unsigned int v = 0; v < mesh->mNumVertices; v++)
        {
            VERTEX_3D vertex = {};

            // 位置に変換行列を適用
            Vector3 pos(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            pos = Vector3::Transform(pos, globalTransform);

            vertex.position = pos;

            if (mesh->HasNormals())
            {
                Vector3 normal(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
                // 法線は回転のみ適用（スケールを無視）
                normal = Vector3::TransformNormal(normal, globalTransform);
                normal.Normalize();
                vertex.normal = normal;
            }

            if (mesh->mTextureCoords[0])
            {
                vertex.uv.x = mesh->mTextureCoords[0][v].x;
                vertex.uv.y = mesh->mTextureCoords[0][v].y;
            }

            vertex.color = Vector4(1, 1, 1, 1);

            vertices.push_back(vertex);
        }

        // インデックス
        for (unsigned int f = 0; f < mesh->mNumFaces; f++)
        {
            aiFace& face = mesh->mFaces[f];
            for (unsigned int idx = 0; idx < face.mNumIndices; idx++)
            {
                indices.push_back(face.mIndices[idx]);
            }
        }

        // Mesh作成
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

    // 子ノードを再帰処理
    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        ProcessNode(node->mChildren[i], scene, device, subMeshes, globalTransform);
    }
}

// Draw, SetMaterial, GetMaterial は変更なし
void Model::Draw(Renderer& renderer, Transform* transform)
{
    for (auto& sub : m_SubMeshes)
    {
        Material* mat = nullptr;
        if (sub.materialIndex >= 0 && sub.materialIndex < (int)m_Materials.size())
        {
            mat = m_Materials[sub.materialIndex].get();
        }
        renderer.DrawMesh(sub.mesh.get(), transform, mat);
    }
}

void Model::SetMaterial(int index, std::shared_ptr<Material> material)
{
    if (index >= 0 && index < (int)m_Materials.size())
    {
        m_Materials[index] = material;
    }
}

Material* Model::GetMaterial(int index) const
{
    if (index >= 0 && index < (int)m_Materials.size())
    {
        return m_Materials[index].get();
    }
    return nullptr;
}