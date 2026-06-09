#include "TestScene.h"
#include "ResourceManager.h"
#include "ResourcePaths.h"
#include "Application.h"
#include "DebugManager.h"
#include "imgui.h"
#include <iostream>

void TestScene::Init()
{
    std::cout << "[TestScene] Init - Full PBR Test" << std::endl;

    auto* device = Application::Get().GetGraphics().GetDevice();
    auto* context = Application::Get().GetGraphics().GetContext();

    m_Camera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 10000.0f);
    m_Camera.SetPosition({ 0.0f, 2.0f, -8.0f });
    m_Camera.SetTarget({ 0.0f, 0.0f, 0.0f });
    SetCamera(&m_Camera);

    // 粒子・VFX
    if (!m_GPUParticleSystem.Initialize(device, context, 100000))
    {
        std::cout << "[Error] GPU Particle init failed" << std::endl;
        return;
    }
    m_GPUParticleSystem.SetCamera(&m_Camera);

    m_ParticleTexture = ResourceManager::Get().LoadTexture(Res::Tex::ParticleSheet);
    if (m_ParticleTexture)
        m_GPUParticleSystem.SetTexture(m_ParticleTexture);

    m_Context.particleSystem = &m_GPUParticleSystem;
    m_Effect.InitStateMachine(m_Context);
    m_Editor.SetEffect(&m_Effect);
    m_Editor.SetContext(m_Context);
    m_Editor.SetTexture(m_ParticleTexture);
    m_Editor.SetParticleSystem(&m_GPUParticleSystem);

    // ===== Shadowkin（フル PBR）=====
    m_Model = ResourceManager::Get().LoadModel(Res::Mdl::Shadowkin);

    if (m_Model)
    {
        auto pbrVS = ResourceManager::Get().LoadVS(L"PBR_VS", Res::Shd::PBR_VS);
        auto pbrPS = ResourceManager::Get().LoadPS(L"PBR_PS", Res::Shd::PBR_PS);

        if (!pbrVS) std::cout << "[Error] PBR_VS failed\n";
        if (!pbrPS) std::cout << "[Error] PBR_PS failed\n";

        // フル PBR 材质を作るヘルパー（5枚全部）
        auto makePBR = [&](const wchar_t* alb, const wchar_t* nrm, const wchar_t* met,
            const wchar_t* rgh, const wchar_t* ao)
            {
                auto m = std::make_shared<Material>();
                m->SetVertexShader(pbrVS);
                m->SetPixelShader(pbrPS);
                m->SetAlbedoTexture(ResourceManager::Get().LoadTexture(alb));
                m->SetNormalTexture(ResourceManager::Get().LoadTexture(nrm));
                m->SetMetallicTexture(ResourceManager::Get().LoadTexture(met));
                m->SetRoughnessTexture(ResourceManager::Get().LoadTexture(rgh));
                m->SetAOTexture(ResourceManager::Get().LoadTexture(ao));
                return m;
            };

        auto silverMat = makePBR(
            Res::Tex::Silver_Albedo, Res::Tex::Silver_Normal,
            Res::Tex::Silver_Metallic, Res::Tex::Silver_Roughness, Res::Tex::Silver_AO);

        auto pantsMat = makePBR(
            Res::Tex::Pants_Albedo, Res::Tex::Pants_Normal,
            Res::Tex::Pants_Metallic, Res::Tex::Pants_Roughness, Res::Tex::Pants_AO);

        // ★ material index はコンソールの [Material N] name: を見て調整 ★
        // とりあえず 0=Silver, 1=Pants と仮定して割り当て。
        // print を見て、Silver/Pants が逆なら入れ替える。
        int matCount = (int)m_Model->GetMaterialCount();
        std::cout << "[Shadowkin] material count = " << matCount << "\n";

        if (matCount >= 1) m_Model->SetMaterial(0, silverMat);
        if (matCount >= 2) m_Model->SetMaterial(1, pantsMat);
        // 材质が3つ以上ある場合、残りも適当に割り当て（表示確認用）
        for (int i = 2; i < matCount; ++i)
            m_Model->SetMaterial(i, silverMat);
    }
    else
    {
        std::cout << "[Error] Shadowkin load failed\n";
    }

    std::cout << "[TestScene] Init complete" << std::endl;

    m_Skybox.Init(device, Res::Mdl::SkyboxSphere, Res::Tex::SkyboxPanorama);
}

void TestScene::Shutdown()
{
    m_Effect.Stop();
    std::cout << "[TestScene] Shutdown" << std::endl;
}

void TestScene::Update(float dt)
{
    SceneBase::Update(dt);

    m_TotalTime += dt;
    m_Effect.Update(dt);

    m_ModelTransform.SetPosition({ m_ModelPos[0], m_ModelPos[1], m_ModelPos[2] });
    m_ModelTransform.SetRotation({ m_ModelRot[0], m_ModelRot[1], m_ModelRot[2] });
    m_ModelTransform.SetScale({ m_ModelScale[0], m_ModelScale[1], m_ModelScale[2] });

    if (m_Renderer)
    {
        m_Renderer->SetDirectionalLight(
            Vector3(m_LightDir[0], m_LightDir[1], m_LightDir[2]),
            Vector3(m_LightColor[0], m_LightColor[1], m_LightColor[2]),
            m_LightIntensity);
        m_Renderer->SetAmbientColor(
            Vector3(m_AmbientColor[0], m_AmbientColor[1], m_AmbientColor[2]));
    }

    DrawDebugUI();
    m_Editor.Draw();
}

void TestScene::Render(Renderer& renderer)
{
    SceneBase::Render(renderer);
    m_Skybox.Render(renderer, GetCamera());
    m_Renderer = &renderer;

    if (m_Model)
        m_Model->Draw(renderer, &m_ModelTransform);

    m_GPUParticleSystem.SetCamera(GetCamera());
    m_GPUParticleSystem.Render();
}

void TestScene::DrawDebugUI()
{
    ImGui::Begin("PBR Test Control");

    if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat3("Position", m_ModelPos, 0.05f);
        ImGui::DragFloat3("Rotation", m_ModelRot, 1.0f);
        ImGui::DragFloat3("Scale", m_ModelScale, 0.01f, 0.001f, 1000.0f);

        if (ImGui::Button("Reset Model"))
        {
            m_ModelPos[0] = m_ModelPos[1] = m_ModelPos[2] = 0.0f;
            m_ModelRot[0] = m_ModelRot[1] = m_ModelRot[2] = 0.0f;
            m_ModelScale[0] = m_ModelScale[1] = m_ModelScale[2] = 1.0f;
        }

        if (m_Model)
        {
            Vector3 mn = m_Model->GetBoundsMin();
            Vector3 mx = m_Model->GetBoundsMax();
            ImGui::Text("Size: %.2f, %.2f, %.2f", mx.x - mn.x, mx.y - mn.y, mx.z - mn.z);
        }
    }

    if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat3("Direction", m_LightDir, 0.02f, -1.0f, 1.0f);
        ImGui::ColorEdit3("Light Color", m_LightColor);
        ImGui::DragFloat("Intensity", &m_LightIntensity, 0.05f, 0.0f, 10.0f);
        ImGui::Separator();
        ImGui::ColorEdit3("Ambient Color", m_AmbientColor);

        if (ImGui::Button("Reset Light"))
        {
            m_LightDir[0] = 0.5f; m_LightDir[1] = -1.0f; m_LightDir[2] = 0.5f;
            m_LightColor[0] = m_LightColor[1] = m_LightColor[2] = 1.0f;
            m_LightIntensity = 1.0f;
            m_AmbientColor[0] = m_AmbientColor[1] = m_AmbientColor[2] = 0.2f;
        }
    }

    ImGui::End();
}