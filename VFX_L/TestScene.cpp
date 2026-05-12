#include "TestScene.h"
#include "ModelRenderer.h"
#include "ResourceManager.h"
#include "Application.h"
#include "imgui.h"
#include <iostream>

void TestScene::Init()
{
    std::cout << "[TestScene] Init" << std::endl;

    auto* device = Application::Get().GetGraphics().GetDevice();
    auto* context = Application::Get().GetGraphics().GetContext();

    // ===== Camera =====
    m_Camera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 1000.0f);
    m_Camera.SetPosition({ 0.0f, 5.0f, -15.0f });
    m_Camera.SetTarget({ 0.0f, 0.0f, 0.0f });
    SetCamera(&m_Camera);


    // ===== Particle System =====
    m_ParticleSystem.Initialize(10000);

    ParticleEmitter& emitter = m_ParticleSystem.GetEmitter();
    emitter.position = { 0.0f, 0.0f, 0.0f };
    emitter.direction = { 0.0f, 1.0f, 0.0f };
    emitter.emitRate = 50.0f;
    emitter.spreadAngle = 30.0f;
    emitter.speedMin = 2.0f;
    emitter.speedMax = 5.0f;
    emitter.sizeMin = 0.1f;
    emitter.sizeMax = 0.3f;
    emitter.lifetimeMin = 1.0f;
    emitter.lifetimeMax = 2.0f;
    emitter.startColor = { 1.0f, 0.8f, 0.2f, 1.0f };
    emitter.endColor = { 1.0f, 0.2f, 0.0f, 0.0f };
    emitter.gravity = { 0.0f, -2.0f, 0.0f };

    // ===== Particle Renderer =====
    m_ParticleRenderer.Initialize(device, context, 10000);
    m_ParticleRenderer.SetCamera(&m_Camera);

    m_ParticleTexture = ResourceManager::Get().LoadTexture(L"Assets/Particles/Particle.png");
    if (m_ParticleTexture)
    {
        m_ParticleRenderer.SetTexture(m_ParticleTexture);
    }

    std::cout << "[TestScene] Init complete" << std::endl;
}

void TestScene::Shutdown()
{
    std::cout << "[TestScene] Shutdown" << std::endl;
    m_ParticleRenderer.Shutdown();
}

void TestScene::Update(float dt)
{
    SceneBase::Update(dt);

    m_ParticleSystem.Update(dt);

    // ImGui
    ImGuiParticle();
    ImGuiModel();
    ImGuiLight();
}

void TestScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);
    m_ParticleRenderer.Render(m_ParticleSystem.GetParticles());
}

// ===== ImGui =====

void TestScene::ImGuiParticle()
{
    ImGui::Begin("Particle");

    ParticleEmitter& em = m_ParticleSystem.GetEmitter();

    ImGui::Text("Alive: %d", m_ParticleSystem.GetAliveCount());
    ImGui::Separator();

    // Emitter
    ImGui::DragFloat3("Position", &em.position.x, 0.1f);
    ImGui::DragFloat3("Direction", &em.direction.x, 0.01f);
    ImGui::Separator();

    // Emit
    ImGui::SliderFloat("Rate", &em.emitRate, 0.0f, 200.0f);
    ImGui::SliderFloat("Spread", &em.spreadAngle, 0.0f, 180.0f);
    ImGui::Separator();

    // Range
    ImGui::DragFloatRange2("Speed", &em.speedMin, &em.speedMax, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloatRange2("Size", &em.sizeMin, &em.sizeMax, 0.01f, 0.01f, 2.0f);
    ImGui::DragFloatRange2("Life", &em.lifetimeMin, &em.lifetimeMax, 0.1f, 0.1f, 10.0f);
    ImGui::Separator();

    // Color
    ImGui::ColorEdit4("Start", &em.startColor.x);
    ImGui::ColorEdit4("End", &em.endColor.x);
    ImGui::Separator();

    // Physics
    ImGui::DragFloat3("Gravity", &em.gravity.x, 0.1f);
    ImGui::Separator();

    // Control
    bool emitting = m_ParticleSystem.IsEmitting();
    if (ImGui::Checkbox("Emitting", &emitting))
    {
        m_ParticleSystem.SetEmitting(emitting);
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        m_ParticleSystem.Reset();
    }

    ImGui::End();
}

void TestScene::ImGuiModel()
{
    if (!m_ModelObject)
        return;

    ImGui::Begin("Model");

    auto& t = m_ModelObject->GetTransform();

    Vector3 pos = t.GetPosition();
    if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
        t.SetPosition(pos);

    Vector3 rot = t.GetRotation();
    if (ImGui::DragFloat3("Rotation", &rot.x, 1.0f))
        t.SetRotation(rot);

    Vector3 scale = t.GetScale();
    if (ImGui::DragFloat3("Scale", &scale.x, 0.1f))
        t.SetScale(scale);

    ImGui::End();
}

void TestScene::ImGuiLight()
{
    ImGui::Begin("Light");

    auto& renderer = Application::Get().GetRenderer();

    bool changed = false;

    changed |= ImGui::DragFloat3("Direction", &m_LightDir.x, 0.01f);
    changed |= ImGui::ColorEdit3("Color", &m_LightColor.x);
    changed |= ImGui::SliderFloat("Intensity", &m_LightIntensity, 0.0f, 2.0f);

    if (changed)
        renderer.SetDirectionalLight(m_LightDir, m_LightColor, m_LightIntensity);

    if (ImGui::ColorEdit3("Ambient", &m_AmbientColor.x))
        renderer.SetAmbientColor(m_AmbientColor);

    ImGui::End();
}