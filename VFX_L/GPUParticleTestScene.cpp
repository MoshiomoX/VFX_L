#include "GPUParticleTestScene.h"
#include "ResourceManager.h"
#include "Application.h"
#include "ModelRenderer.h"
#include "imgui.h"
#include <iostream>

void GPUParticleTestScene::Init()
{
    std::cout << "[GPUParticleTestScene] Init" << std::endl;

    auto* device = Application::Get().GetGraphics().GetDevice();
    auto* context = Application::Get().GetGraphics().GetContext();

    // ===== Camera =====
    m_Camera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 1000.0f);
    m_Camera.SetPosition({ 0.0f, 5.0f, -15.0f });
    m_Camera.SetTarget({ 0.0f, 0.0f, 0.0f });
    SetCamera(&m_Camera);


    // ===== Model =====
     m_Model = ResourceManager::Get().LoadModel("Assets/Model/house/house.fbx");
     
     m_ModelObject = CreateObject();
     auto* mr = m_ModelObject->AddComponent<ModelRenderer>();
     mr->SetModel(m_Model.get());
	 m_ModelObject->GetTransform().SetPosition({ 0.0f, 0.0f, 30.0f });
	 m_ModelObject->GetTransform().SetScale({ 0.1f, 0.1f, 0.1f });
	 m_ModelObject->GetTransform().SetRotation({ 0.0f, 0.0f, 0.0f });

    // ===== GPU Particle System =====
    if (!m_GPUParticleSystem.Initialize(device, context, 100000))
    {
        std::cout << "[Error] GPU Particle System init failed" << std::endl;
        return;
    }

    m_GPUParticleSystem.SetCamera(&m_Camera);

    // ===== テクスチャ =====
    m_ParticleTexture = ResourceManager::Get().LoadTexture(L"Assets/Particles/circle_05.png");
    if (m_ParticleTexture)
    {
        m_GPUParticleSystem.SetTexture(m_ParticleTexture);
    }

    // ===== Emitter追加 =====
    m_EmitterID = m_GPUParticleSystem.AddEmitter(100.0f, 10000);

    if (auto* e = m_GPUParticleSystem.GetEmitter(m_EmitterID))
    {
        e->emitType = EmitType::Point;
        e->position = { 0.0f, 0.0f, 0.0f };
        e->direction = { 0.0f, 1.0f, 0.0f };
        e->shape.spreadAngle = 30.0f;
        e->speedRange = { 2.0f, 5.0f };
        e->lifetimeRange = { 1.0f, 3.0f };
        e->sizeRange = { 0.1f, 0.3f, 0.0f, 0.05f };
        e->startColorMin = { 1.0f, 0.8f, 0.2f, 1.0f };
        e->startColorMax = { 1.0f, 0.9f, 0.3f, 1.0f };
        e->endColorMin = { 1.0f, 0.2f, 0.0f, 0.0f };
        e->endColorMax = { 1.0f, 0.3f, 0.0f, 0.0f };
        e->gravity = { 0.0f, -2.0f, 0.0f };
        e->dragCoeff = 0.0f;
    }

    std::cout << "[GPUParticleTestScene] Init complete" << std::endl;
}

void GPUParticleTestScene::Shutdown()
{
    std::cout << "[GPUParticleTestScene] Shutdown" << std::endl;
}

void GPUParticleTestScene::Update(float dt)
{
    SceneBase::Update(dt);

    m_TotalTime += dt;
    m_GPUParticleSystem.Update(dt, m_TotalTime);

    ImGuiGPUParticle();
}

void GPUParticleTestScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);
    m_GPUParticleSystem.SetCamera(GetCamera());
    m_GPUParticleSystem.Render();
}

// ===== ImGui =====
void GPUParticleTestScene::ImGuiGPUParticle()
{
    ImGui::Begin("GPU Particle");

    // システム情報
    ImGui::Text("Alive: %u", m_GPUParticleSystem.GetAliveCount());
    ImGui::Text("Dead:  %u", m_GPUParticleSystem.GetDeadCount());
    ImGui::Text("Max:   %u", m_GPUParticleSystem.GetMaxParticles());
    ImGui::Text("Emitters: %d", m_GPUParticleSystem.GetEmitterCount());
    ImGui::Separator();

    auto* e = m_GPUParticleSystem.GetEmitter(m_EmitterID);
    if (!e)
    {
        ImGui::End();
        return;
    }

    // 形状タイプ
    const char* shapeNames[] = { "Point", "Sphere", "Cone", "Box", "Ring", "Disc", "Mesh" };
    int currentType = static_cast<int>(e->emitType);
    if (ImGui::Combo("Shape", &currentType, shapeNames, IM_ARRAYSIZE(shapeNames)))
    {
        e->emitType = static_cast<EmitType>(currentType);
    }

    // 形状パラメータ（タイプによって表示切替）
    switch (e->emitType)
    {
    case EmitType::Point:
        ImGui::SliderFloat("Spread", &e->shape.spreadAngle, 0.0f, 180.0f);
        break;
    case EmitType::Sphere:
        ImGui::DragFloat("Radius", &e->shape.radius, 0.1f, 0.0f, 50.0f);
        break;
    case EmitType::Cone:
        ImGui::SliderFloat("Spread", &e->shape.spreadAngle, 0.0f, 90.0f);
        ImGui::DragFloat("Radius", &e->shape.radius, 0.1f, 0.0f, 50.0f);
        break;
    case EmitType::Box:
        ImGui::DragFloat3("Extents", &e->shape.boxExtents.x, 0.1f, 0.0f, 50.0f);
        break;
    case EmitType::Ring:
        ImGui::DragFloat("Outer Radius", &e->shape.radius, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("Inner Radius", &e->shape.innerRadius, 0.1f, 0.0f, 50.0f);
        break;
    case EmitType::Disc:
        ImGui::DragFloat("Radius", &e->shape.radius, 0.1f, 0.0f, 50.0f);
        ImGui::SliderFloat("Spread", &e->shape.spreadAngle, 0.0f, 180.0f);
        break;
    default:
        break;
    }
    ImGui::Separator();

    // 位置・方向
    ImGui::DragFloat3("Position", &e->position.x, 0.1f);
    ImGui::DragFloat3("Direction", &e->direction.x, 0.01f);
    ImGui::Separator();

    // 発射
    ImGui::SliderFloat("Rate", &e->emitRate, 0.0f, 1000.0f);
    ImGui::DragFloat2("Speed", &e->speedRange.x, 0.1f, 0.0f, 50.0f);
    ImGui::DragFloat2("Lifetime", &e->lifetimeRange.x, 0.1f, 0.1f, 10.0f);
    ImGui::Separator();

    // サイズ
    ImGui::DragFloat4("Size (sMin sMax eMin eMax)", &e->sizeRange.x, 0.01f, 0.0f, 5.0f);
    ImGui::Separator();

    // 色
    ImGui::ColorEdit4("Start Min", &e->startColorMin.x);
    ImGui::ColorEdit4("Start Max", &e->startColorMax.x);
    ImGui::ColorEdit4("End Min", &e->endColorMin.x);
    ImGui::ColorEdit4("End Max", &e->endColorMax.x);
    ImGui::Separator();

    // 物理
    ImGui::DragFloat3("Gravity", &e->gravity.x, 0.1f);
    ImGui::DragFloat("Drag", &e->dragCoeff, 0.01f, 0.0f, 5.0f);
    ImGui::Separator();

    // 回転
    ImGui::DragFloat2("Rotation", &e->rotationRange.x, 0.01f);
    ImGui::DragFloat2("Angular Vel", &e->angularVelRange.x, 0.01f);
    ImGui::Separator();

    // 有効/無効
    bool active = e->IsActive();
    if (ImGui::Checkbox("Active", &active))
    {
        e->SetActive(active);
    }

    ImGui::End();
}