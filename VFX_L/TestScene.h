#pragma once
#include "SceneBase.h"
#include "CameraBase.h"
#include "GPUParticleSystem.h"
#include "VFXEffect.h"
#include "VFXEditor.h"
#include "EntryType.h"
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
    CameraBase m_Camera;

    GPUParticleSystem m_GPUParticleSystem;
    VFXEffect m_Effect;
    VFXContext m_Context;
    VFXEditor m_Editor;
    std::shared_ptr<Texture> m_ParticleTexture;

    float m_TotalTime = 0.0f;
};