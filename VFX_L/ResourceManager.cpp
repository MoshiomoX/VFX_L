#include "ResourceManager.h"
#include <iostream>
#include <thread>  // 新添加：异步 (std::async)

// 新添加：假设一个简单日志函数（可替换为 spdlog 或 cout）
void LogError(const std::wstring& msg) {
    std::wcerr << L"[Error] ResourceManager: " << msg << std::endl;
}

void ResourceManager::Initialize(ID3D11Device* device)
{
    m_Device = device;
    std::cout << "[OK] ResourceManager initialized" << std::endl;
}

void ResourceManager::Shutdown()
{
    CleanupUnused();  // 新添加：释放前清理未用资源
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
    {
        std::wcout << L"[Cache Hit] Texture: " << filepath << std::endl;
        return it->second;
    }

    auto texture = std::make_shared<Texture>();
    bool success = texture->Load(m_Device, filepath);

    if (success)
    {
        std::wcout << L"[OK] Texture loaded successfully: " << filepath << std::endl;
        m_Textures[filepath] = texture;
        return texture;
    }
    else
    {
        std::wcout << L"[ERROR] Texture load FAILED: " << filepath << std::endl;
        // 可以加 Windows API 错误码
        wchar_t buf[256];
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 256, nullptr);
        std::wcout << L"  Windows Error: " << buf << std::endl;

        return nullptr;
    }
}


std::future<std::shared_ptr<Texture>> ResourceManager::LoadTextureAsync(const std::wstring& filepath)
{
    return std::async(std::launch::async, [this, filepath]() {
        return LoadTexture(filepath);  // 调用同步版本，但在后台线程
        });
}

void ResourceManager::UnloadTexture(const std::wstring& filepath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);  // 新添加：线程安全锁
    m_Textures.erase(filepath);
}

// ===== Shader =====
std::shared_ptr<Shader> ResourceManager::LoadShader(
    const std::wstring& name,
    const std::wstring& vsPath,
    const std::wstring& psPath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);  // 新添加：线程安全锁

    // キャッシュ確認
    auto it = m_Shaders.find(name);
    if (it != m_Shaders.end())
    {
        return it->second;
    }

    // 新規読み込み
    auto shader = std::make_shared<Shader>();
    if (!shader->Load(m_Device, vsPath, psPath))
    {
        LogError(L"Failed to load shader: " + name);  // 新添加：增强错误处理
        return nullptr;
    }

    m_Shaders[name] = shader;
    return shader;
}

void ResourceManager::UnloadShader(const std::wstring& name)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);  // 新添加：线程安全锁
    m_Shaders.erase(name);
}

// ===== Mesh =====
std::shared_ptr<Mesh> ResourceManager::LoadMesh(
    const std::wstring& name,
    const std::vector<VERTEX_3D>& vertices,
    const std::vector<unsigned int>& indices)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);  // 新添加：线程安全锁

    // キャッシュ確認
    auto it = m_Meshes.find(name);
    if (it != m_Meshes.end())
    {
        return it->second;
    }

    // 新規作成
    auto mesh = std::make_shared<Mesh>();
    if (!mesh->Create(m_Device, vertices, indices))
    {
        LogError(L"Failed to create mesh: " + name);  // 新添加：增强错误处理
        return nullptr;
    }

    m_Meshes[name] = mesh;
    return mesh;
}

void ResourceManager::UnloadMesh(const std::wstring& name)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);  // 新添加：线程安全锁
    m_Meshes.erase(name);
}

// 新添加：资源依赖 - LoadMaterial 示例（依赖 Shader + Texture）
std::shared_ptr<Material> ResourceManager::LoadMaterial(
    const std::wstring& name,
    const std::wstring& shaderName,
    const std::wstring& texturePath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);  // 新添加：线程安全锁

    auto it = m_Materials.find(name);
    if (it != m_Materials.end())
    {
        return it->second;
    }

    // 加载依赖资源（自动处理缓存）
    auto shader = LoadShader(shaderName, L"Shader/VS.hlsl", L"Shader/PS.hlsl");  // 假设路径固定，可参数化
    if (!shader) {
        LogError(L"Failed to load shader for material: " + name);
        return nullptr;
    }

    auto texture = LoadTexture(texturePath);
    if (!texture) {
        LogError(L"Failed to load texture for material: " + name);
        return nullptr;
    }

    // 创建 Material
    auto material = std::make_shared<Material>();
    material->SetShader(shader);
    material->SetTexture(texture);

    m_Materials[name] = material;
    return material;
}

// ===== 全解放 =====
void ResourceManager::UnloadAll()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);  // 新添加：线程安全锁
    m_Textures.clear();
    m_Shaders.clear();
    m_Meshes.clear();
    m_Materials.clear();
    m_Models.clear();
    std::cout << "[OK] All resources unloaded" << std::endl;
}

void ResourceManager::CleanupUnused()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    // Textures
    for (auto it = m_Textures.begin(); it != m_Textures.end(); ) {
        if (it->second.use_count() == 1) {
            std::wcout << L"[Info] Cleaning unused texture: " << it->first << std::endl;
            it = m_Textures.erase(it);
        }
        else {
            ++it;
        }
    }

    // Models（追加）
    for (auto it = m_Models.begin(); it != m_Models.end(); ) {
        if (it->second.use_count() == 1) {
            std::cout << "[Info] Cleaning unused model: " << it->first << std::endl;
            it = m_Models.erase(it);
        }
        else {
            ++it;
        }
    }
}

//// 新添加：调试 - 估算内存使用（假设每个类有 GetSize() 方法）
size_t ResourceManager::EstimateMemoryUsage() const
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);  // 新添加：线程安全锁
    size_t total = 0;
    for (const auto& pair : m_Textures)
    {
    //   total += pair.second->GetSize();  // 假设 Texture::GetSize() 返回字节数
    }
    // 类似加其他 map
    // ...
    return total;
}

std::shared_ptr<Model> ResourceManager::LoadModel(const std::string& filepath)
{
    std::cout << "[LoadModel] Start: " << filepath << std::endl;
    std::cout << "[LoadModel] Device: " << (m_Device ? "OK" : "nullptr") << std::endl;

    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    // キャッシュ確認
    auto it = m_Models.find(filepath);
    if (it != m_Models.end())
    {
        std::cout << "[LoadModel] Cache hit" << std::endl;
        return it->second;
    }

    std::cout << "[LoadModel] Creating new model..." << std::endl;

    // 新規読み込み
    auto model = std::make_shared<Model>();
    if (!model->Load(m_Device, filepath))
    {
        std::cout << "[LoadModel] Load FAILED" << std::endl;
        return nullptr;
    }

    std::cout << "[LoadModel] Load SUCCESS" << std::endl;
    m_Models[filepath] = model;
    return model;
}


void ResourceManager::UnloadModel(const std::string& filepath)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_Models.erase(filepath);
}