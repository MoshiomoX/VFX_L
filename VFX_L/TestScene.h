#pragma once
#include "SceneBase.h"
#include "CameraBase.h"
#include "GPUParticleSystem.h"
#include "ParticleEffect.h"
#include <memory>
#include "Texture.h"

class TestScene : public SceneBase
{
public:
    void Init() override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    void ImGuiEffectEditor();

private:
    CameraBase m_Camera;

    GPUParticleSystem m_GPUParticleSystem;
    ParticleEffect m_Effect;
    std::shared_ptr<Texture> m_ParticleTexture;

    float m_TotalTime = 0.0f;
};