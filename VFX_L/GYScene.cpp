#include "GYScene.h"
#include "ResourceManager.h"
#include "ResourcePaths.h"
#include "Application.h"
#include "imgui.h"
#include <iostream>

void GYScene::Init()
{
    std::cout << "[GYScene] Init - Particle Scene" << std::endl;

    auto* device = Application::Get().GetGraphics().GetDevice();
    auto* context = Application::Get().GetGraphics().GetContext();

    // ==================== 1. Camera 初期化 ====================
    m_Camera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 10000.0f);
    m_Camera.SetPosition({ 0.0f, 2.0f, -8.0f });
    m_Camera.SetTarget({ 0.0f, 0.0f, 0.0f });
    SetCamera(&m_Camera);

    // ==================== 2. Particle & VFX 初期化 ====================
    if (!m_GPUParticleSystem.Initialize(device, context, 100000))
    {
        std::cout << "[Error] GPU Particle init failed" << std::endl;
        return;
    }
    m_GPUParticleSystem.SetCamera(&m_Camera);

    m_ParticleTexture = ResourceManager::Get().LoadTexture(Res::Tex::ParticleSheet);
    if (m_ParticleTexture)
        m_GPUParticleSystem.SetTexture(m_ParticleTexture);

    // VFX 一式（Effect / Context / Editor）を接続
    m_Context.particleSystem = &m_GPUParticleSystem;
    m_Effect.InitStateMachine(m_Context);
    m_Editor.SetEffect(&m_Effect);
    m_Editor.SetContext(m_Context);
    m_Editor.SetTexture(m_ParticleTexture);
    m_Editor.SetParticleSystem(&m_GPUParticleSystem);

   // // ==================== 3. Skybox 初期化 ====================
   // m_Skybox.Init(device, Res::Mdl::SkyboxSphere, Res::Tex::SkyboxPanorama);

    std::cout << "[GYScene] Init complete" << std::endl;
}

void GYScene::Shutdown()
{
    m_Effect.Stop();
    std::cout << "[GYScene] Shutdown" << std::endl;
}

void GYScene::Update(float dt)
{
    SceneBase::Update(dt);

    m_TotalTime += dt;
    m_Effect.Update(dt);

    m_Editor.Draw();
}

void GYScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);

   // // Skybox（カメラ追従）
   // m_Skybox.Render(renderer, GetCamera());

    // GPU 粒子
    m_GPUParticleSystem.SetCamera(GetCamera());
    m_GPUParticleSystem.Render();
}