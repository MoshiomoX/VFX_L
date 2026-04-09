#pragma once
#include "Component.h"
#include "Model.h"

class ModelRenderer : public Component
{
public:
    void Init() override {}
    void Update(float dt) override {}

    void SetModel(Model* model) { m_Model = model; }
    Model* GetModel() const { return m_Model; }

    void Draw(class Renderer& renderer);

private:
    Model* m_Model = nullptr;
};