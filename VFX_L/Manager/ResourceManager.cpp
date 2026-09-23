// ============================================================
// ResourceManager.cpp
// ============================================================
#include "Manager/ResourceManager.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Model/SkinnedModel.h"
#include "VFX_Editor/VFXTextureRef.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <wrl/client.h>
#include <filesystem>
#include <iostream>

using Microsoft::WRL::ComPtr;

// ============================================================
// entry point の警告
// cso モードでは entry は cso 生成時に決まっている（main 固定）。
// 呼ぶ側が別の名前を要求しても無視されるので、その旨を出す
// ============================================================
static void WarnIfCustomEntry(const std::wstring& name, const std::string& entry)
{
    if (entry.empty() || entry == "main") return;

    std::wcout << L"[Warning] entry point is ignored in cso mode: " << name
        << L" (requested \"" << std::wstring(entry.begin(), entry.end()) << L"\")"
        << std::endl;
}

static bool SceneHasBones(const aiScene* scene)
{
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        if (scene->mMeshes[i]->HasBones())
            return true;
    return false;
}

// ============================================================
// 骨の有無で static / skinned を振り分ける
// ============================================================
LoadedModel ResourceManager::LoadModelAuto(const std::string& filepath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    LoadedModel out;

    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals |
        aiProcess_MakeLeftHanded |
        aiProcess_PopulateArmatureData);   // 骨 / armature の情報を埋める

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        std::cout << "[LoadModelAuto] Assimp error: " << importer.GetErrorString() << std::endl;
        return out;
    }

    namespace fs = std::filesystem;
    const size_t lastSlash = filepath.find_last_of("/\\");
    const std::string dir = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash + 1) : "";
    const std::string name = fs::path(filepath).stem().string();

    if (SceneHasBones(scene))
    {
        out.kind = ModelKind::Skinned;
        out.skinnedModel = std::make_shared<SkinnedModel>();
        out.skinnedModel->LoadFromScene(m_Device, scene, dir, name);
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

// ============================================================
// Texture
// ============================================================
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

std::shared_ptr<Texture> ResourceManager::LoadEmbeddedTexture(const std::wstring& key,
    const void* data, size_t size, const char* formatHint)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_Textures.find(key);
    if (it != m_Textures.end())
        return it->second;

    auto texture = std::make_shared<Texture>();
    if (texture->LoadFromMemory(m_Device, data, size, formatHint))
    {
        m_Textures[key] = texture;
        return texture;
    }

    std::wcout << L"[Error] Embedded texture load failed: " << key << std::endl;
    return nullptr;
}

// ============================================================
// ノイズ生成（NoiseGenCS）
// 配方の内容を key に cache する。R32_FLOAT 1 チャンネル
// （typed UAV store が FL11.0 で保証されている形式）
// ============================================================
std::shared_ptr<Texture> ResourceManager::LoadNoiseTexture(const NoiseRecipe& recipe)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    const std::string keyA = recipe.CacheKey();
    const std::wstring key(keyA.begin(), keyA.end());
    auto it = m_Textures.find(key);
    if (it != m_Textures.end())
        return it->second;

    auto cs = LoadCS(L"NoiseGenCS", L"Shader/VFX/NoiseGenCS.hlsl");
    if (!cs) return nullptr;

    const UINT size = (UINT)(recipe.size < 8 ? 8 : recipe.size);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = size;
    td.Height = size;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &tex))) return nullptr;
    if (FAILED(m_Device->CreateShaderResourceView(tex.Get(), nullptr, &srv))) return nullptr;
    if (FAILED(m_Device->CreateUnorderedAccessView(tex.Get(), nullptr, &uav))) return nullptr;

    // HLSL の NoiseCB と同じ並び（32B）
    struct NoiseCB
    {
        uint32_t type, size, frequency, octaves;
        float persistence;
        uint32_t seed, pad0, pad1;
    } cb = {};
    cb.type = (uint32_t)recipe.type;
    cb.size = size;
    cb.frequency = (uint32_t)(recipe.frequency < 1 ? 1 : recipe.frequency);
    cb.octaves = (uint32_t)(recipe.octaves < 1 ? 1 : recipe.octaves);
    cb.persistence = recipe.persistence;
    cb.seed = recipe.seed;

    ComPtr<ID3D11DeviceContext> ctx;
    m_Device->GetImmediateContext(&ctx);

    cs->WriteBuffer(ctx.Get(), 0, &cb);
    cs->Bind(ctx.Get());
    cs->SetUAV(ctx.Get(), "dst", uav.Get());
    cs->BindUAVs(ctx.Get());
    ctx->Dispatch((size + 7) / 8, (size + 7) / 8, 1);
    cs->UnbindUAVs(ctx.Get());

    auto texture = std::make_shared<Texture>();
    texture->Adopt(srv.Get(), (int)size, (int)size);
    m_Textures[key] = texture;

    std::cout << "[OK] Noise baked: " << keyA << std::endl;
    return texture;
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

// ============================================================
// VertexShader
// Debug / Release とも cso を読む。hlslPath は ShaderPath::ToCso で
// 出力先の cso に変換される（entry は cso 側で固定）
// ============================================================
std::shared_ptr<VertexShader> ResourceManager::LoadVS(
    const std::wstring& name, const std::wstring& hlslPath, const std::string& entry)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    auto it = m_VertexShaders.find(name);
    if (it != m_VertexShaders.end()) return it->second;

    WarnIfCustomEntry(name, entry);

    auto vs = std::make_shared<VertexShader>();
    const std::string csoPath = ShaderPath::ToCso(hlslPath);

    if (FAILED(vs->Load(m_Device, csoPath.c_str())))
    {
        std::wcout << L"[Error] VS load failed: " << name
            << L" (" << std::wstring(csoPath.begin(), csoPath.end()) << L")" << std::endl;
        return nullptr;
    }

    m_VertexShaders[name] = vs;
    return vs;
}

std::shared_ptr<VertexShader> ResourceManager::LoadVS_CSO(
    const std::wstring& name, const std::string& csoPath)
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

// ============================================================
// PixelShader
// ============================================================
std::shared_ptr<PixelShader> ResourceManager::LoadPS(
    const std::wstring& name, const std::wstring& hlslPath, const std::string& entry)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    auto it = m_PixelShaders.find(name);
    if (it != m_PixelShaders.end()) return it->second;

    WarnIfCustomEntry(name, entry);

    auto ps = std::make_shared<PixelShader>();
    const std::string csoPath = ShaderPath::ToCso(hlslPath);

    if (FAILED(ps->Load(m_Device, csoPath.c_str())))
    {
        std::wcout << L"[Error] PS load failed: " << name
            << L" (" << std::wstring(csoPath.begin(), csoPath.end()) << L")" << std::endl;
        return nullptr;
    }

    m_PixelShaders[name] = ps;
    return ps;
}

std::shared_ptr<PixelShader> ResourceManager::LoadPS_CSO(
    const std::wstring& name, const std::string& csoPath)
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

// ============================================================
// ComputeShader
// ============================================================
std::shared_ptr<ComputeShader> ResourceManager::LoadCS(
    const std::wstring& name, const std::wstring& hlslPath, const std::string& entry)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    auto it = m_ComputeShaders.find(name);
    if (it != m_ComputeShaders.end()) return it->second;

    WarnIfCustomEntry(name, entry);

    auto cs = std::make_shared<ComputeShader>();
    const std::string csoPath = ShaderPath::ToCso(hlslPath);

    if (FAILED(cs->Load(m_Device, csoPath.c_str())))
    {
        std::wcout << L"[Error] CS load failed: " << name
            << L" (" << std::wstring(csoPath.begin(), csoPath.end()) << L")" << std::endl;
        return nullptr;
    }

    m_ComputeShaders[name] = cs;
    return cs;
}

std::shared_ptr<ComputeShader> ResourceManager::LoadCS_CSO(
    const std::wstring& name, const std::string& csoPath)
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

// ============================================================
// Mesh
// ============================================================
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

// ============================================================
// Material
// ============================================================
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

// ============================================================
// Model（静的）
// ============================================================
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

// ============================================================
// 一括解放
// ============================================================
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

// ============================================================
// VFX テンプレート
// ============================================================
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