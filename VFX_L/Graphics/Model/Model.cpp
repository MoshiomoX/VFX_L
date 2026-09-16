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
#include "ResourcePaths.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;
using namespace DirectX::SimpleMath;

// ============================================================
// テクスチャの実体を探す
//   そのまま → 同じ階層 → textures/ → Textures/ → Assets/
// ============================================================
static std::wstring FindTexturePath(const std::string& directory, const std::string& texPath)
{
    auto toW = [](const std::string& s) { return std::wstring(s.begin(), s.end()); };

    if (fs::exists(texPath)) return toW(texPath);

    const std::string filename = fs::path(texPath).filename().string();
    const std::string candidates[] = {
        directory + filename,
        directory + "textures/" + filename,
        directory + "Textures/" + filename,
        "Assets/" + filename,
    };
    for (const auto& p : candidates)
        if (fs::exists(p)) return toW(p);

    std::cout << "[Warning] Texture not found: " << texPath << std::endl;
    return L"";
}

// ============================================================
// マテリアルから1枚のテクスチャを取る
//   candidates を順に試し、最初に見つかった物を返す。
//   埋め込み（FBX / GLB）なら scene から取り出す。
//   外部ファイルなら FindTexturePath で探す。
//   見つからなければ nullptr（Material::Bind が既定色を入れる）
// ============================================================
static std::shared_ptr<Texture> LoadMaterialTexture(
    ID3D11Device* device, const aiScene* scene, const aiMaterial* mat,
    const std::string& directory, const std::wstring& modelKey,
    std::initializer_list<aiTextureType> candidates)
{
    for (aiTextureType type : candidates)
    {
        if (mat->GetTextureCount(type) == 0) continue;

        aiString path;
        if (mat->GetTexture(type, 0, &path) != AI_SUCCESS) continue;

        // ---- 埋め込み ----
        if (const aiTexture* emb = scene->GetEmbeddedTexture(path.C_Str()))
        {
            const std::string p = path.C_Str();
            const std::wstring key = modelKey + L"#" + std::wstring(p.begin(), p.end());

            if (emb->mHeight == 0)
            {
                // 圧縮ファイルそのまま（png/jpg など）。mWidth がバイト数
                return ResourceManager::Get().LoadEmbeddedTexture(
                    key, emb->pcData, emb->mWidth, emb->achFormatHint);
            }

            // 非圧縮 BGRA。稀なので cache しない
            auto tex = std::make_shared<Texture>();
            if (tex->CreateFromMemory(device, emb->pcData, emb->mWidth, emb->mHeight,
                DXGI_FORMAT_B8G8R8A8_UNORM))
                return tex;
            return nullptr;
        }

        // ---- 外部ファイル ----
        const std::wstring found = FindTexturePath(directory, path.C_Str());
        if (!found.empty())
        {
            if (auto tex = ResourceManager::Get().LoadTexture(found))
                return tex;
        }
    }
    return nullptr;
}

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

    auto defaultVS = ResourceManager::Get().LoadVS(L"Default", L"Shader/VS.hlsl");
    auto defaultPS = ResourceManager::Get().LoadPS(L"Default", L"Shader/PS.hlsl");
    auto pbrVS = ResourceManager::Get().LoadVS(L"PBR_VS", Res::Shd::PBR_VS);
    auto pbrPS = ResourceManager::Get().LoadPS(L"PBR_PS", Res::Shd::PBR_PS);

    // 埋め込みテクスチャの cache key（同じモデルを2回読んでも解凍は1回）
    const std::wstring modelKey =
        std::wstring(directory.begin(), directory.end()) +
        std::wstring(modelName.begin(), modelName.end());

    // ---------- マテリアル ----------
    for (unsigned int i = 0; i < scene->mNumMaterials; i++)
    {
        aiMaterial* aiMat = scene->mMaterials[i];
        auto material = std::make_shared<Material>();

        // 各スロットを候補順に探す。
        // glTF は基色が BASE_COLOR、metal/rough 合図が UNKNOWN に来る。
        // 古い OBJ / FBX は法線を HEIGHT に入れてくる事がある
        auto albedo = LoadMaterialTexture(device, scene, aiMat, m_Directory, modelKey,
            { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });
        auto normal = LoadMaterialTexture(device, scene, aiMat, m_Directory, modelKey,
            { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT });
        auto metallic = LoadMaterialTexture(device, scene, aiMat, m_Directory, modelKey,
            { aiTextureType_METALNESS, aiTextureType_UNKNOWN });
        auto roughness = LoadMaterialTexture(device, scene, aiMat, m_Directory, modelKey,
            { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN, aiTextureType_SHININESS });
        auto ao = LoadMaterialTexture(device, scene, aiMat, m_Directory, modelKey,
            { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP });

        // 基色が無ければ「モデル名.png」を探す（従来の救済）
        if (!albedo)
        {
            const char* exts[] = { ".png", ".jpg", ".jpeg", ".tga", ".dds" };
            for (const char* ext : exts)
            {
                const std::string p = m_Directory + modelName + ext;
                if (!fs::exists(p)) continue;
                albedo = ResourceManager::Get().LoadTexture(std::wstring(p.begin(), p.end()));
                if (albedo) break;
            }
        }

        material->SetAlbedoTexture(albedo);
        material->SetNormalTexture(normal);
        material->SetMetallicTexture(metallic);
        material->SetRoughnessTexture(roughness);
        material->SetAOTexture(ao);

        // シェーダー選択: 法線か金属/粗さのどれかがあれば PBR
        const bool usePBR = (normal || metallic || roughness) && pbrVS && pbrPS;
        material->SetVertexShader(usePBR ? pbrVS : defaultVS);
        material->SetPixelShader(usePBR ? pbrPS : defaultPS);

        // 色（今は PS が読まない。Material に CB を付けた時に繋ぐ）
        aiColor4D color;
        if (aiMat->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS ||
            aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
            material->SetColor(Vector4(color.r, color.g, color.b, color.a));

        std::cout << "[Model] Material " << i << ": " << (usePBR ? "PBR" : "Lambert")
            << "  albedo=" << (albedo ? "y" : "-")
            << " normal=" << (normal ? "y" : "-")
            << " metal=" << (metallic ? "y" : "-")
            << " rough=" << (roughness ? "y" : "-")
            << " ao=" << (ao ? "y" : "-") << std::endl;

        m_Materials.push_back(material);
    }

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