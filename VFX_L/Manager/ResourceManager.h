#pragma once
#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#include "Graphics/Material/Texture.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Material/Material.h"
#include "Graphics/Model/Model.h"
#include "VFX_Editor/VFXEffect.h"


struct aiScene;
struct NoiseRecipe;

class SkinnedModel;

enum class ModelKind { Static, Skinned };
struct LoadedModel
{
    ModelKind                     kind = ModelKind::Static;
    std::shared_ptr<Model>        staticModel;
    std::shared_ptr<SkinnedModel> skinnedModel;
};
class ResourceManager
{
public:
    static ResourceManager& Get()
    {
        static ResourceManager instance;
        return instance;
    }

    void Initialize(ID3D11Device* device);
    void Shutdown();

    // Texture
    std::shared_ptr<Texture> LoadTexture(const std::wstring& filepath);
    std::future<std::shared_ptr<Texture>> LoadTextureAsync(const std::wstring& filepath);
    std::shared_ptr<Texture> LoadNoiseTexture(const NoiseRecipe& recipe);
    void UnloadTexture(const std::wstring& filepath);

    // VertexShader
    std::shared_ptr<VertexShader> LoadVS(const std::wstring& name,
        const std::wstring& hlslPath,
        const std::string& entry = "main");
    std::shared_ptr<VertexShader> LoadVS_CSO(const std::wstring& name,
        const std::string& csoPath);

    // PixelShader
    std::shared_ptr<PixelShader> LoadPS(const std::wstring& name,
        const std::wstring& hlslPath,
        const std::string& entry = "main");
    std::shared_ptr<PixelShader> LoadPS_CSO(const std::wstring& name,
        const std::string& csoPath);

    // ComputeShader
    std::shared_ptr<ComputeShader> LoadCS(const std::wstring& name,
        const std::wstring& hlslPath,
        const std::string& entry = "main");
    std::shared_ptr<ComputeShader> LoadCS_CSO(const std::wstring& name,
        const std::string& csoPath);

    // Mesh
    std::shared_ptr<Mesh> LoadMesh(const std::wstring& name,
        const std::vector<VERTEX_3D>& vertices,
        const std::vector<unsigned int>& indices);
    void UnloadMesh(const std::wstring& name);

    // Material
    std::shared_ptr<Material> LoadMaterial(const std::wstring& name,
        const std::wstring& vsName,
        const std::wstring& psName,
        const std::wstring& texturePath);

    // 埋め込みテクスチャ（assimp の aiTexture）。key で cache する
    std::shared_ptr<Texture> LoadEmbeddedTexture(const std::wstring& key,
        const void* data, size_t size, const char* formatHint);

    // Model
    std::shared_ptr<Model> LoadModel(const std::string& filepath);
    void UnloadModel(const std::string& filepath);

    void UnloadAll();
    void CleanupUnused();

    size_t GetTextureCount() const { return m_Textures.size(); }
    size_t GetVSCount() const { return m_VertexShaders.size(); }
    size_t GetPSCount() const { return m_PixelShaders.size(); }
    size_t GetCSCount() const { return m_ComputeShaders.size(); }
    size_t GetModelCount() const { return m_Models.size(); }
    // 骨の有無で static / skinned を振り分けて読む。パスで cache する。
    // 先読み中（PreloadModelsAsync）のパスなら完了を待って同じ物を返す
    LoadedModel LoadModelAuto(const std::string& filepath);

    // ============================================================
    // モデルの先読み（起動時に呼ぶ）
    // 別スレッドで assimp import と D3D バッファ作成までやる。
    //   D3D11 の device はスレッド安全、immediate context は触らない。
    //   SkinnedModel::LoadFromScene は context を使わないのでそのまま乗る。
    //   m_Mutex は map の出し入れの間だけ握る（import 中に握ると
    //   主スレッドの LoadTexture 等が止まって本末転倒）
    // ============================================================
    void PreloadModelsAsync(const std::vector<std::string>& paths);
    bool IsPreloadDone() const;            // 全部終わったか（進捗表示用）
    int  GetPreloadPending() const;        // まだ終わっていない数

    std::shared_ptr<VFXEffect> LoadVFXTemplate(const std::string& filepath);
    void UnloadVFXTemplate(const std::string& filepath);
    size_t GetVFXCount() const { return m_VFXTemplates.size(); }

private:
    ResourceManager() = default;
    ~ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

private:
    ID3D11Device* m_Device = nullptr;

    std::unordered_map<std::wstring, std::shared_ptr<Texture>> m_Textures;
    std::unordered_map<std::wstring, std::shared_ptr<VertexShader>> m_VertexShaders;
    std::unordered_map<std::wstring, std::shared_ptr<PixelShader>> m_PixelShaders;
    std::unordered_map<std::wstring, std::shared_ptr<ComputeShader>> m_ComputeShaders;
    std::unordered_map<std::wstring, std::shared_ptr<Mesh>> m_Meshes;
    std::unordered_map<std::wstring, std::shared_ptr<Material>> m_Materials;
    std::unordered_map<std::string, std::shared_ptr<Model>> m_Models;
    std::unordered_map<std::string, std::shared_ptr<VFXEffect>> m_VFXTemplates;
    mutable std::recursive_mutex m_Mutex;

    // ---- LoadModelAuto の cache と先読み ----
    // cache 済みは m_AutoModels、読み込み中は m_AutoPending（shared_future なので
    // 複数の呼び手が同じ結果を待てる）。import 本体は lock 無しで走る
    LoadedModel ImportModelAuto(const std::string& filepath);   // cache を見ない生の読み込み
    std::unordered_map<std::string, LoadedModel> m_AutoModels;
    std::unordered_map<std::string, std::shared_future<LoadedModel>> m_AutoPending;
    std::vector<std::thread> m_PreloadThreads;
};