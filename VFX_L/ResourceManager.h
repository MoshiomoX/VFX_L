#pragma once
#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <future>     
#include <mutex>      
#include "Texture.h"
#include "Shader.h"
#include "Mesh.h"
#include "Material.h"
#include "Model.h" 

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

    // Shader
    std::shared_ptr<Shader> LoadShader(
        const std::wstring& name,
        const std::wstring& vsPath,
        const std::wstring& psPath);
    void UnloadShader(const std::wstring& name);

    // Mesh
    std::shared_ptr<Mesh> LoadMesh(
        const std::wstring& name,
        const std::vector<VERTEX_3D>& vertices,
        const std::vector<unsigned int>& indices);
    void UnloadMesh(const std::wstring& name);

    // Material
    std::shared_ptr<Material> LoadMaterial(
        const std::wstring& name,
        const std::wstring& shaderName,
        const std::wstring& texturePath);

    // Model（追加）
    std::shared_ptr<Model> LoadModel(const std::string& filepath);
    void UnloadModel(const std::string& filepath);

    // 全解放
    void UnloadAll();

    // 释放策略
    void CleanupUnused();

    // デバッグ用
    size_t GetTextureCount() const { return m_Textures.size(); }
    size_t GetShaderCount() const { return m_Shaders.size(); }
    size_t GetMeshCount() const { return m_Meshes.size(); }
    size_t GetMaterialCount() const { return m_Materials.size(); }
    size_t GetModelCount() const { return m_Models.size(); }  // 追加
    size_t EstimateMemoryUsage() const;

private:
    ResourceManager() = default;
    ~ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

private:
    ID3D11Device* m_Device = nullptr;

    std::unordered_map<std::wstring, std::shared_ptr<Texture>> m_Textures;
    std::unordered_map<std::wstring, std::shared_ptr<Shader>> m_Shaders;
    std::unordered_map<std::wstring, std::shared_ptr<Mesh>> m_Meshes;
    std::unordered_map<std::wstring, std::shared_ptr<Material>> m_Materials;
    std::unordered_map<std::string, std::shared_ptr<Model>> m_Models;  // 追加（string因为Assimp用string）

    mutable std::recursive_mutex m_Mutex;
};