#pragma once
#include "SceneBase.h"
#include "CameraBase.h"
#include "GPUParticleSystem.h"
#include "VFXEffect.h"
#include "VFXEditor.h"
#include "Skybox.h"

// ============================================================
// GYScene
// 課題用の粒子シーン。カメラ / Skybox / GPU粒子 / VFXエディタ を
// TestScene から流用し、PBR・骨骼は含まない。
// ============================================================
class GYScene : public SceneBase
{
public:
    void Init() override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    // カメラ
    CameraBase m_Camera;

    // Particle & VFX（TestScene と同じ一式）
    GPUParticleSystem        m_GPUParticleSystem;
    VFXEffect                m_Effect;
    VFXContext               m_Context;
    VFXEditor                m_Editor;
    std::shared_ptr<Texture> m_ParticleTexture;

    // Skybox
    Skybox m_Skybox;

    // 時間管理
    float m_TotalTime = 0.0f;
};