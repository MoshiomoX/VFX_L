#pragma once
#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <future>
#include <mutex>
#include "Texture.h"
#include "VertexShader.h"
#include "PixelShader.h"
#include "ComputeShader.h"
#include "Mesh.h"
#include "Material.h"
#include "Model.h"


struct aiScene;
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
    LoadedModel LoadModelAuto(const std::string& filepath);


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

    mutable std::recursive_mutex m_Mutex;
};