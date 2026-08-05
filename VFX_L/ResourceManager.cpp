#include "ResourceManager.h"
#include <iostream>
#include "SkinnedModel.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h> 
#include <filesystem>


static std::string ToCsoPath(const std::wstring& hlslPath)
{
    std::string s(hlslPath.begin(), hlslPath.end());
    size_t dot = s.find_last_of('.');
    if (dot != std::string::npos)
        s = s.substr(0, dot);
    return s + ".cso";   // パス保持、拡張子だけ差し替え
}
static bool SceneHasBones(const aiScene* scene)
{
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        if (scene->mMeshes[i]->HasBones())
            return true;
    return false;
}
LoadedModel ResourceManager::LoadModelAuto(const std::string& filepath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    LoadedModel out;

    // --- import は1回だけ。static/skinned 両対応 ---
    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals |
        aiProcess_MakeLeftHanded | /*
        aiProcess_LimitBoneWeights |*/
        aiProcess_PopulateArmatureData);   // ★追加：骨/アーマチュア情報を整える（先生コード参照）

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        std::cout << "[LoadModelAuto] Assimp失敗: " << importer.GetErrorString() << std::endl;
        return out;
    }

    namespace fs = std::filesystem;
    size_t lastSlash = filepath.find_last_of("/\\");
    std::string dir = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash + 1) : "";
    std::string name = fs::path(filepath).stem().string();

    if (SceneHasBones(scene))
    {
        out.kind = ModelKind::Skinned;
        out.skinnedModel = std::make_shared<SkinnedModel>();
        out.skinnedModel->LoadFromScene(scene, dir);
        std::cout << "[LoadModelAuto] -> Skinned : " << filepath << std::endl;
    }
    else
    {
        out.kind = ModelKind::Static;
        out.staticModel = std::make_shared<Model>();
        out.staticModel->LoadFromScene(m_Device, scene, dir, name);
        std::cout << "[LoadModelAuto] -> Static : " << filepath << std::endl;
    }

    return out;
}
void ResourceManager::Initialize(ID3D11Device* device)
{
    m_Device = device;
    std::cout << "[OK] ResourceManager initialized" << std::endl;
}

void ResourceManager::Shutdown()
{
    CleanupUnused();
    UnloadAll();
    m_Device = nullptr;
    std::cout << "[OK] ResourceManager shutdown" << std::endl;
}

// ===== Texture =====
std::shared_ptr<Texture> ResourceManager::LoadTexture(const std::wstring& filepath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_Textures.find(filepath);
    if (it != m_Textures.end())
        return it->second;

    auto texture = std::make_shared<Texture>();
    if (texture->Load(m_Device, filepath))
    {
        m_Textures[filepath] = texture;
        return texture;
    }

    std::wcout << L"[Error] Texture load failed: " << filepath << std::endl;
    return nullptr;
}

std::future<std::shared_ptr<Texture>> ResourceManager::LoadTextureAsync(const std::wstring& filepath)
{
    return std::async(std::launch::async, [this, filepath]() {
        return LoadTexture(filepath);
        });
}

void ResourceManager::UnloadTexture(const std::wstring& filepath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_Textures.erase(filepath);
}

// ===== VertexShader =====
std::shared_ptr<VertexShader> ResourceManager::LoadVS(
    const std::wstring& name, const std::wstring& hlslPath, const std::string& entry)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    auto it = m_VertexShaders.find(name);
    if (it != m_VertexShaders.end()) return it->second;

    auto vs = std::make_shared<VertexShader>();
#ifdef _DEBUG
    HRESULT hr = vs->Compile(m_Device, hlslPath, entry);
#else
    HRESULT hr = vs->Load(m_Device, ToCsoPath(hlslPath).c_str());
#endif
    if (FAILED(hr)) { std::wcout << L"[Error] VS load failed: " << name << std::endl; return nullptr; }
    m_VertexShaders[name] = vs;
    return vs;
}

std::shared_ptr<VertexShader> ResourceManager::LoadVS_CSO(
    const std::wstring& name,
    const std::string& csoPath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_VertexShaders.find(name);
    if (it != m_VertexShaders.end())
        return it->second;

    auto vs = std::make_shared<VertexShader>();
    if (FAILED(vs->Load(m_Device, csoPath.c_str())))
    {
        std::wcout << L"[Error] VS cso load failed: " << name << std::endl;
        return nullptr;
    }

    m_VertexShaders[name] = vs;
    return vs;
}

// ===== PixelShader =====
std::shared_ptr<PixelShader> ResourceManager::LoadPS(
    const std::wstring& name, const std::wstring& hlslPath, const std::string& entry)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    auto it = m_PixelShaders.find(name);
    if (it != m_PixelShaders.end()) return it->second;

    auto ps = std::make_shared<PixelShader>();
#ifdef _DEBUG
    HRESULT hr = ps->Compile(m_Device, hlslPath, entry);
#else
    HRESULT hr = ps->Load(m_Device, ToCsoPath(hlslPath).c_str());
#endif
    if (FAILED(hr)) { std::wcout << L"[Error] PS load failed: " << name << std::endl; return nullptr; }
    m_PixelShaders[name] = ps;
    return ps;
}
std::shared_ptr<PixelShader> ResourceManager::LoadPS_CSO(
    const std::wstring& name,
    const std::string& csoPath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_PixelShaders.find(name);
    if (it != m_PixelShaders.end())
        return it->second;

    auto ps = std::make_shared<PixelShader>();
    if (FAILED(ps->Load(m_Device, csoPath.c_str())))
    {
        std::wcout << L"[Error] PS cso load failed: " << name << std::endl;
        return nullptr;
    }

    m_PixelShaders[name] = ps;
    return ps;
}

// ===== ComputeShader =====
std::shared_ptr<ComputeShader> ResourceManager::LoadCS(
    const std::wstring& name, const std::wstring& hlslPath, const std::string& entry)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    auto it = m_ComputeShaders.find(name);
    if (it != m_ComputeShaders.end()) return it->second;

    auto cs = std::make_shared<ComputeShader>();
#ifdef _DEBUG
    HRESULT hr = cs->Compile(m_Device, hlslPath, entry);
#else
    HRESULT hr = cs->Load(m_Device, ToCsoPath(hlslPath).c_str());
#endif
    if (FAILED(hr)) { std::wcout << L"[Error] CS load failed: " << name << std::endl; return nullptr; }
    m_ComputeShaders[name] = cs;
    return cs;
}

std::shared_ptr<ComputeShader> ResourceManager::LoadCS_CSO(
    const std::wstring& name,
    const std::string& csoPath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_ComputeShaders.find(name);
    if (it != m_ComputeShaders.end())
        return it->second;

    auto cs = std::make_shared<ComputeShader>();
    if (FAILED(cs->Load(m_Device, csoPath.c_str())))
    {
        std::wcout << L"[Error] CS cso load failed: " << name << std::endl;
        return nullptr;
    }

    m_ComputeShaders[name] = cs;
    return cs;
}

// ===== Mesh =====
std::shared_ptr<Mesh> ResourceManager::LoadMesh(
    const std::wstring& name,
    const std::vector<VERTEX_3D>& vertices,
    const std::vector<unsigned int>& indices)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_Meshes.find(name);
    if (it != m_Meshes.end())
        return it->second;

    auto mesh = std::make_shared<Mesh>();
    if (!mesh->Create(m_Device, vertices, indices))
    {
        std::wcout << L"[Error] Mesh create failed: " << name << std::endl;
        return nullptr;
    }

    m_Meshes[name] = mesh;
    return mesh;
}

void ResourceManager::UnloadMesh(const std::wstring& name)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_Meshes.erase(name);
}

// ===== Material =====
std::shared_ptr<Material> ResourceManager::LoadMaterial(
    const std::wstring& name,
    const std::wstring& vsName,
    const std::wstring& psName,
    const std::wstring& texturePath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_Materials.find(name);
    if (it != m_Materials.end())
        return it->second;

    auto vsIt = m_VertexShaders.find(vsName);
    auto psIt = m_PixelShaders.find(psName);

    if (vsIt == m_VertexShaders.end() || psIt == m_PixelShaders.end())
    {
        std::wcout << L"[Error] Shader not found for material: " << name << std::endl;
        return nullptr;
    }

    auto texture = LoadTexture(texturePath);

    auto material = std::make_shared<Material>();
    material->SetVertexShader(vsIt->second);
    material->SetPixelShader(psIt->second);
    if (texture)
        material->SetTexture(texture);

    m_Materials[name] = material;
    return material;
}

// ===== Model =====
std::shared_ptr<Model> ResourceManager::LoadModel(const std::string& filepath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_Models.find(filepath);
    if (it != m_Models.end())
        return it->second;

    auto model = std::make_shared<Model>();
    if (!model->Load(m_Device, filepath))
    {
        std::cout << "[Error] Model load failed: " << filepath << std::endl;
        return nullptr;
    }

    m_Models[filepath] = model;
    return model;
}

void ResourceManager::UnloadModel(const std::string& filepath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_Models.erase(filepath);
}

// ===== 全解放 =====
void ResourceManager::UnloadAll()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_Textures.clear();
    m_VertexShaders.clear();
    m_PixelShaders.clear();
    m_ComputeShaders.clear();
    m_Meshes.clear();
    m_Materials.clear();
    m_Models.clear();
    std::cout << "[OK] All resources unloaded" << std::endl;
}

void ResourceManager::CleanupUnused()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    for (auto it = m_Textures.begin(); it != m_Textures.end(); )
    {
        if (it->second.use_count() == 1)
            it = m_Textures.erase(it);
        else
            ++it;
    }

    for (auto it = m_Models.begin(); it != m_Models.end(); )
    {
        if (it->second.use_count() == 1)
            it = m_Models.erase(it);
        else
            ++it;
    }
}

// ===== VFX テンプレート =====
std::shared_ptr<VFXEffect> ResourceManager::LoadVFXTemplate(const std::string& filepath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_VFXTemplates.find(filepath);
    if (it != m_VFXTemplates.end())
        return it->second;

    auto effect = std::make_shared<VFXEffect>();
    if (!effect->LoadFromFile(filepath))
    {
        std::cout << "[Error] VFX template load failed: " << filepath << std::endl;
        return nullptr;
    }

    m_VFXTemplates[filepath] = effect;
    std::cout << "[OK] VFX template cached: " << filepath << std::endl;
    return effect;
}

void ResourceManager::UnloadVFXTemplate(const std::string& filepath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_VFXTemplates.erase(filepath);
}

