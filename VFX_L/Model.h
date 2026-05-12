#pragma once
#include <vector>
#include <memory>
#include <string>
#include <d3d11.h>
#include "Mesh.h"
#include "Material.h"
#include "Transform.h"

class Renderer;

class Model
{
public:
    struct SubMesh  
    {
        std::shared_ptr<Mesh> mesh;
        int materialIndex = -1;
    };

    bool Load(ID3D11Device* device, const std::string& filepath);
    void Draw(Renderer& renderer, Transform* transform);

    size_t GetSubMeshCount() const { return m_SubMeshes.size(); }
    size_t GetMaterialCount() const { return m_Materials.size(); }

    void SetMaterial(int index, std::shared_ptr<Material> material);
    Material* GetMaterial(int index) const;

private:
    std::vector<SubMesh> m_SubMeshes;
    std::vector<std::shared_ptr<Material>> m_Materials;
    std::string m_Directory;
};