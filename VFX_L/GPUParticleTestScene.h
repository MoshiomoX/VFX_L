#pragma once
#include "SceneBase.h"
#include "CameraBase.h"
#include "GPUParticleSystem.h"
#include <memory>
#include "Model.h"


class GPUParticleTestScene : public SceneBase
{
public:
    void Init() override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    void ImGuiGPUParticle();

private:
    CameraBase m_Camera;


    // Model
    std::shared_ptr<Model> m_Model;
    GameObject* m_ModelObject = nullptr;

    // GPU粒子
    GPUParticleSystem m_GPUParticleSystem;
    std::shared_ptr<Texture> m_ParticleTexture;
    int m_EmitterID = -1;

    // 時間管理
    float m_TotalTime = 0.0f;
};