// ============================================================
// MaterialLoader.cpp
// ============================================================
#include "Graphics/Model/MaterialLoader.h"
#include "Graphics/Material/Material.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include <assimp/scene.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace DirectX::SimpleMath;

namespace
{
    std::wstring ToW(const std::string& s) { return std::wstring(s.begin(), s.end()); }

    // テクスチャの実体を探す: そのまま → 同じ階層 → textures/ → Textures/ → Assets/
    std::wstring FindTexturePath(const std::string& directory, const std::string& texPath)
    {
        if (fs::exists(texPath)) return ToW(texPath);

        const std::string filename = fs::path(texPath).filename().string();
        const std::string candidates[] = {
            directory + filename,
            directory + "textures/" + filename,
            directory + "Textures/" + filename,
            "Assets/" + filename,
        };
        for (const auto& p : candidates)
            if (fs::exists(p)) return ToW(p);

        std::cout << "[Warning] Texture not found: " << texPath << std::endl;
        return L"";
    }

    // マテリアルから1枚。candidates を順に試し、埋め込み / 外部の両方に対応
    std::shared_ptr<Texture> LoadMaterialTexture(
        ID3D11Device* device, const aiScene* scene, const aiMaterial* mat,
        const std::string& directory, const std::wstring& modelKey,
        std::initializer_list<aiTextureType> candidates)
    {
        for (aiTextureType type : candidates)
        {
            if (mat->GetTextureCount(type) == 0) continue;

            aiString path;
            if (mat->GetTexture(type, 0, &path) != AI_SUCCESS) continue;

            if (const aiTexture* emb = scene->GetEmbeddedTexture(path.C_Str()))
            {
                const std::wstring key = modelKey + L"#" + ToW(path.C_Str());
                if (emb->mHeight == 0)
                    return ResourceManager::Get().LoadEmbeddedTexture(
                        key, emb->pcData, emb->mWidth, emb->achFormatHint);

                auto tex = std::make_shared<Texture>();
                if (tex->CreateFromMemory(device, emb->pcData, emb->mWidth, emb->mHeight,
                    DXGI_FORMAT_B8G8R8A8_UNORM))
                    return tex;
                return nullptr;
            }

            const std::wstring found = FindTexturePath(directory, path.C_Str());
            if (!found.empty())
                if (auto tex = ResourceManager::Get().LoadTexture(found))
                    return tex;
        }
        return nullptr;
    }
}

std::vector<std::shared_ptr<Material>> MaterialLoader::LoadFromScene(
    ID3D11Device* device, const aiScene* scene,
    const std::string& directory, const std::string& modelName,
    std::shared_ptr<VertexShader> vsOverride)
{
    std::vector<std::shared_ptr<Material>> out;

    auto defaultVS = ResourceManager::Get().LoadVS(L"Default", L"Shader/VS.hlsl");
    auto defaultPS = ResourceManager::Get().LoadPS(L"Default", L"Shader/PS.hlsl");
    auto pbrVS = ResourceManager::Get().LoadVS(L"PBR_VS", Res::Shd::PBR_VS);
    auto pbrPS = ResourceManager::Get().LoadPS(L"PBR_PS", Res::Shd::PBR_PS);

    const std::wstring modelKey = ToW(directory) + ToW(modelName);

    for (unsigned int i = 0; i < scene->mNumMaterials; i++)
    {
        aiMaterial* aiMat = scene->mMaterials[i];
        auto material = std::make_shared<Material>();

        auto albedo = LoadMaterialTexture(device, scene, aiMat, directory, modelKey,
            { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });
        auto normal = LoadMaterialTexture(device, scene, aiMat, directory, modelKey,
            { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT });
        auto metallic = LoadMaterialTexture(device, scene, aiMat, directory, modelKey,
            { aiTextureType_METALNESS, aiTextureType_UNKNOWN });
        auto roughness = LoadMaterialTexture(device, scene, aiMat, directory, modelKey,
            { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN, aiTextureType_SHININESS });
        auto ao = LoadMaterialTexture(device, scene, aiMat, directory, modelKey,
            { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP });

        if (!albedo)
        {
            const char* exts[] = { ".png", ".jpg", ".jpeg", ".tga", ".dds" };
            for (const char* ext : exts)
            {
                const std::string p = directory + modelName + ext;
                if (!fs::exists(p)) continue;
                albedo = ResourceManager::Get().LoadTexture(ToW(p));
                if (albedo) break;
            }
        }

        material->SetAlbedoTexture(albedo);
        material->SetNormalTexture(normal);
        material->SetMetallicTexture(metallic);
        material->SetRoughnessTexture(roughness);
        material->SetAOTexture(ao);

        const bool usePBR = (normal || metallic || roughness) && pbrPS;
        material->SetVertexShader(vsOverride ? vsOverride : (usePBR ? pbrVS : defaultVS));
        material->SetPixelShader(usePBR ? pbrPS : defaultPS);

        aiColor4D color;
        if (aiMat->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS ||
            aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
            material->SetColor(Vector4(color.r, color.g, color.b, color.a));

        std::cout << "[Material] " << modelName << " #" << i << ": " << (usePBR ? "PBR" : "Lambert")
            << (vsOverride ? " (skinned)" : "")
            << "  albedo=" << (albedo ? "y" : "-")
            << " normal=" << (normal ? "y" : "-")
            << " metal=" << (metallic ? "y" : "-")
            << " rough=" << (roughness ? "y" : "-")
            << " ao=" << (ao ? "y" : "-") << std::endl;

        out.push_back(material);
    }
    return out;
}