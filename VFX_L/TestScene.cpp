#include "TestScene.h"
#include "ResourceManager.h"
#include "Application.h"
#include "DebugManager.h"
#include <iostream>

void TestScene::Init()
{
    std::cout << "[TestScene] Init - VFX Editor" << std::endl;

    auto* device = Application::Get().GetGraphics().GetDevice();
    auto* context = Application::Get().GetGraphics().GetContext();

    m_Camera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 10000.0f);
    m_Camera.SetPosition({ 0.0f, 5.0f, -15.0f });
    m_Camera.SetTarget({ 0.0f, 0.0f, 0.0f });
    SetCamera(&m_Camera);

    if (!m_GPUParticleSystem.Initialize(device, context, 100000))
    {
        std::cout << "[Error] GPU Particle System init failed" << std::endl;
        return;
    }
    m_GPUParticleSystem.SetCamera(&m_Camera);

    m_ParticleTexture = ResourceManager::Get().LoadTexture(L"Assets/Particles/particlesSheet.jpg");
    if (m_ParticleTexture)
        m_GPUParticleSystem.SetTexture(m_ParticleTexture);

    m_Context.particleSystem = &m_GPUParticleSystem;


    m_Editor.SetEffect(&m_Effect);
    m_Editor.SetContext(m_Context);
    m_Editor.SetTexture(m_ParticleTexture);
    m_Editor.SetParticleSystem(&m_GPUParticleSystem);

    std::cout << "[TestScene] Init complete" << std::endl;
}

void TestScene::Shutdown()
{
    m_Effect.Stop(m_Context);
    std::cout << "[TestScene] Shutdown" << std::endl;
}

void TestScene::Update(float dt)
{
    SceneBase::Update(dt);

    m_TotalTime += dt;
    m_Effect.Update(dt, m_Context);
   //     m_GPUParticleSystem.Update(dt, m_TotalTime);

    //static uint32_t lastAlive = 0;
    //uint32_t alive = m_GPUParticleSystem.GetAliveCount();
    //if (alive != lastAlive)
    //{
    //    std::cout << "Alive: " << alive << std::endl;
    //    lastAlive = alive;
    //}
    m_Editor.Draw();
   
}

void TestScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);
    m_GPUParticleSystem.SetCamera(GetCamera());
    m_GPUParticleSystem.Render();
}