#pragma once
#include "SceneBase.h"
#include "CameraBase.h"
#include "Model.h"
#include "CPUParticleSystem.h"
#include "ParticleRenderer.h"
#include <memory>

class TestScene : public SceneBase
{
public:
    void Init() override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    // ImGui
    void ImGuiParticle();
    void ImGuiModel();
    void ImGuiLight();

private:

    CameraBase m_Camera;

    // Model
    std::shared_ptr<Model> m_Model;
    GameObject* m_ModelObject = nullptr;

    // Particle
    CPUParticleSystem m_ParticleSystem;
    ParticleRenderer m_ParticleRenderer;
    std::shared_ptr<Texture> m_ParticleTexture;

    // LightÅiImGuiópÅj
    Vector3 m_LightDir = { 0.5f, -1.0f, 0.5f };
    Vector3 m_LightColor = { 1.0f, 1.0f, 1.0f };
    float m_LightIntensity = 1.0f;
    Vector3 m_AmbientColor = { 0.2f, 0.2f, 0.2f };
};