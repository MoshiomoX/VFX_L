// ============================================================
// MaterialLoader.cpp
// ============================================================
#include "Graphics/Model/MaterialLoader.h"
#include "Graphics/Material/Material.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"

#include <assimp/scene.h>
#include <Windows.h>   // MultiByteToWideChar（UTF-8 のパス）

namespace fs = std::filesystem;
using namespace DirectX::SimpleMath;

namespace
{
    std::wstring ToW(const std::string& s) { return std::wstring(s.begin(), s.end()); }

    // assimp の文字列は UTF-8。窄い std::string のまま fs::path にすると ANSI（日本語環境は CP932）として
    // 変換され、作者の PC の中国語入りのパス（shuimian_02.FBX の F:\...\风暴英雄全套特效贴图\...png など）で
    // 例外が飛ぶ。Debug では abort() のダイアログが出たまま固まっていた（2026-10-02）。UTF-8 → 幅広で作る
    fs::path Utf8Path(const std::string& s)
    {
        if (s.empty()) return fs::path();
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return fs::path();
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return fs::path(w);
    }

    // テクスチャの実体を探す: そのまま → 同じ階層 → textures/ → Textures/ → Assets/
    // 存在の確認は error_code 版（読めないドライブ等でも例外にしない）
    std::wstring FindTexturePath(const std::string& directory, const std::string& texPath)
    {
        std::error_code ec;
        const fs::path tex = Utf8Path(texPath);
        if (!tex.empty() && fs::exists(tex, ec)) return tex.wstring();

        const fs::path filename = tex.filename();
        const fs::path dir = Utf8Path(directory);
        const fs::path candidates[] = {
            dir / filename,
            dir / "textures" / filename,
            dir / "Textures" / filename,
            dir / "Tex" / filename,   // Assets/Model/Shadowkin_SF/Tex（2026-10-04）
            fs::path("Assets") / filename,
        };
        for (const auto& p : candidates)
            if (!filename.empty() && fs::exists(p, ec)) return p.wstring();

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
        if (!albedo)
        {
            // テクスチャの参照を持たない FBX（Rock-Set など）: 同梱の色テクスチャを名前で探す。
            // <名前>_Tex/ か同じ目録の「<名前> で始まり Base_Color / BaseColor / Albedo / Diffuse を含む」画像
            auto lower = [](std::string s) { for (auto& ch : s) ch = (char)tolower((unsigned char)ch); return s; };
            const std::string key = lower(modelName);
            for (const std::string& dirPath : { directory + modelName + "_Tex/", directory })
            {
                std::error_code ec;
                if (albedo || !fs::is_directory(dirPath, ec)) continue;
                for (const auto& de : fs::directory_iterator(dirPath, ec))
                {
                    const std::string n = lower(de.path().filename().string());
                    const std::string ext = lower(de.path().extension().string());
                    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".tga") continue;
                    if (n.rfind(key, 0) != 0) continue;
                    if (n.find("base_color") == std::string::npos && n.find("basecolor") == std::string::npos
                        && n.find("albedo") == std::string::npos && n.find("diffuse") == std::string::npos) continue;
                    albedo = ResourceManager::Get().LoadTexture(ToW(de.path().generic_string()));
                    if (albedo) break;
                }
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