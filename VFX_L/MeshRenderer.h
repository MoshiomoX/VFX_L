#pragma once
#include "Component.h"
#include "Mesh.h"
#include "Material.h"

class MeshRenderer: public Component
{
public:
    void Init() override {}
    void Update(float dt) override {}

    void SetMesh(Mesh* mesh) { m_Mesh = mesh; }
    void SetMaterial(Material* material) { m_Material = material; }

    Mesh* GetMesh() const { return m_Mesh; }
    Material* GetMaterial() const { return m_Material; }

    void Draw(class Renderer& renderer);

private:
    Mesh* m_Mesh = nullptr;
    Material* m_Material = nullptr;
};