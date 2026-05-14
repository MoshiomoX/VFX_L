#include "TestScene.h"
#include "ResourceManager.h"
#include "Application.h"
#include "DebugManager.h"
#include "imgui.h"
#include <iostream>

void TestScene::Init()
{
    std::cout << "[TestScene] Init - ParticleEffect Editor" << std::endl;

    auto* device = Application::Get().GetGraphics().GetDevice();
    auto* context = Application::Get().GetGraphics().GetContext();

    // Camera
    m_Camera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 1000.0f);
    m_Camera.SetPosition({ 0.0f, 5.0f, -15.0f });
    m_Camera.SetTarget({ 0.0f, 0.0f, 0.0f });
    SetCamera(&m_Camera);

    // GPU Particle System
    if (!m_GPUParticleSystem.Initialize(device, context, 100000))
    {
        std::cout << "[Error] GPU Particle System init failed" << std::endl;
        return;
    }
    m_GPUParticleSystem.SetCamera(&m_Camera);

    // テクスチャ
    m_ParticleTexture = ResourceManager::Get().LoadTexture(L"Assets/Particles/particlesSheet.jpg");
    if (m_ParticleTexture)
        m_GPUParticleSystem.SetTexture(m_ParticleTexture);

    // 空のEffect
    m_Effect.SetName("NewEffect");

    std::cout << "[TestScene] Init complete" << std::endl;
}
void TestScene::Shutdown()
{
    m_Effect.Stop(&m_GPUParticleSystem);
    std::cout << "[TestScene] Shutdown" << std::endl;
}

void TestScene::Update(float dt)
{
    SceneBase::Update(dt);

    m_TotalTime += dt;
    m_Effect.Update(dt, &m_GPUParticleSystem);
    m_GPUParticleSystem.Update(dt, m_TotalTime);

    ImGuiEffectEditor();
}

void TestScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);
    m_GPUParticleSystem.SetCamera(GetCamera());
    m_GPUParticleSystem.Render();
}

void TestScene::ImGuiEffectEditor()
{
    ImGui::Begin("ParticleEffect Editor");

    ImGui::Text("Alive: %u", m_GPUParticleSystem.GetAliveCount());
    ImGui::Text("Dead:  %u", m_GPUParticleSystem.GetDeadCount());
    ImGui::Text("Max:   %u", m_GPUParticleSystem.GetMaxParticles());
    ImGui::Separator();

    ImGui::Text("Effect: %s", m_Effect.GetName().c_str());
    ImGui::Text("Time: %.2f / %.2f", m_Effect.GetCurrentTime(), m_Effect.GetTotalDuration());
    ImGui::Separator();

    // 再生制御
    if (m_Effect.IsPlaying())
    {
        if (ImGui::Button("Stop"))
            m_Effect.Stop(&m_GPUParticleSystem);
    }
    else
    {
        if (ImGui::Button("Play"))
            m_Effect.Play(&m_GPUParticleSystem);
    }

    ImGui::SameLine();
    bool loop = m_Effect.IsLooping();
    if (ImGui::Checkbox("Loop", &loop))
        m_Effect.SetLooping(loop);

    ImGui::Separator();

    // Entry追加
    if (ImGui::Button("+ Add Entry"))
    {
        m_Effect.AddEntry(0.0f, 2.0f);
    }

    ImGui::Separator();

    // 各Entry編集
    int removeIndex = -1;
    for (int i = 0; i < m_Effect.GetEntryCount(); i++)
    {
        auto* entry = m_Effect.GetEntry(i);
        if (!entry) continue;

        ImGui::PushID(i);

        std::string label = "Entry " + std::to_string(i);
        if (ImGui::CollapsingHeader(label.c_str()))
        {
            // 削除ボタン
            if (ImGui::Button("Delete"))
                removeIndex = i;

            // タイムライン
            ImGui::DragFloat("Start Time", &entry->startTime, 0.1f, 0.0f, 10.0f);
            ImGui::DragFloat("Duration", &entry->duration, 0.1f, -1.0f, 10.0f);
            ImGui::Separator();

            // 形状
            auto& e = entry->emitterData;
            const char* shapeNames[] = { "Point", "Sphere", "Cone", "Box", "Ring", "Disc", "Mesh" };
            int currentType = static_cast<int>(e.emitType);
            if (ImGui::Combo("Shape", &currentType, shapeNames, IM_ARRAYSIZE(shapeNames)))
                e.emitType = static_cast<EmitType>(currentType);

            switch (e.emitType)
            {
            case EmitType::Point:
                ImGui::SliderFloat("Spread", &e.shape.spreadAngle, 0.0f, 180.0f);
                break;
            case EmitType::Sphere:
                ImGui::DragFloat("Radius", &e.shape.radius, 0.1f, 0.0f, 50.0f);
                break;
            case EmitType::Cone:
                ImGui::SliderFloat("Spread", &e.shape.spreadAngle, 0.0f, 90.0f);
                ImGui::DragFloat("Radius", &e.shape.radius, 0.1f, 0.0f, 50.0f);
                break;
            case EmitType::Box:
                ImGui::DragFloat3("Extents", &e.shape.boxExtents.x, 0.1f, 0.0f, 50.0f);
                break;
            case EmitType::Ring:
                ImGui::DragFloat("Outer Radius", &e.shape.radius, 0.1f, 0.0f, 50.0f);
                ImGui::DragFloat("Inner Radius", &e.shape.innerRadius, 0.1f, 0.0f, 50.0f);
                break;
            case EmitType::Disc:
                ImGui::DragFloat("Radius", &e.shape.radius, 0.1f, 0.0f, 50.0f);
                ImGui::SliderFloat("Spread", &e.shape.spreadAngle, 0.0f, 180.0f);
                break;
            default:
                break;
            }
            ImGui::Separator();

            // 位置・方向
            ImGui::DragFloat3("Position", &e.position.x, 0.1f);
            ImGui::DragFloat3("Direction", &e.direction.x, 0.01f);
            ImGui::Separator();

            // 発射
            ImGui::SliderFloat("Rate", &e.emitRate, 0.0f, 1000.0f);
            ImGui::DragInt("Max Particles", &e.maxParticles, 100, 100, 50000);
            ImGui::DragFloat2("Speed", &e.speedRange.x, 0.1f, 0.0f, 50.0f);
            ImGui::DragFloat2("Lifetime", &e.lifetimeRange.x, 0.1f, 0.1f, 10.0f);
            ImGui::Separator();

            // サイズ
            ImGui::DragFloat4("Size (sMin sMax eMin eMax)", &e.sizeRange.x, 0.01f, 0.0f, 5.0f);
            ImGui::Separator();

            // 色
            ImGui::ColorEdit4("Start Min", &e.startColorMin.x);
            ImGui::ColorEdit4("Start Max", &e.startColorMax.x);
            ImGui::ColorEdit4("End Min", &e.endColorMin.x);
            ImGui::ColorEdit4("End Max", &e.endColorMax.x);
            ImGui::Separator();

            // 物理
            ImGui::DragFloat3("Gravity", &e.gravity.x, 0.1f);
            ImGui::DragFloat("Drag", &e.dragCoeff, 0.01f, 0.0f, 5.0f);
            ImGui::Separator();

            // 回転
            ImGui::DragFloat2("Rotation", &e.rotationRange.x, 0.01f);
            ImGui::DragFloat2("Angular Vel", &e.angularVelRange.x, 0.01f);
            ImGui::Separator();

            // Atlas
            ImGui::DragInt("Atlas Rows", &e.atlasRows, 1, 1, 16);
            ImGui::DragInt("Atlas Cols", &e.atlasCols, 1, 1, 16);
            ImGui::Checkbox("Atlas Animate", &e.atlasAnimate);
            if (!e.atlasAnimate)
            {
                int maxIdx = e.atlasRows * e.atlasCols - 1;
                ImGui::SliderInt("Atlas Index", &e.atlasIndex, 0, maxIdx);
            }
        }

        ImGui::PopID();
    }

    // 削除処理（ループ外で実行）
    if (removeIndex >= 0)
    {
        m_Effect.RemoveEntry(removeIndex);
    }

    ImGui::End();
}