#include "TestScene.h"
#include "ResourceManager.h"
#include "ResourcePaths.h"
#include "Application.h"
#include "DebugManager.h"
#include "imgui.h"
#include <iostream>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

void TestScene::Init()
{
    std::cout << "[TestScene] Init - Full PBR Test (Shadowkin)" << std::endl;

    auto* device = Application::Get().GetGraphics().GetDevice();
    auto* context = Application::Get().GetGraphics().GetContext();
    Material::InitDefaultTextures(device);

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

    m_Context.particleSystem = &m_GPUParticleSystem;
    m_Effect.InitStateMachine(m_Context);
    m_Editor.SetEffect(&m_Effect);
    m_Editor.SetContext(m_Context);
    m_Editor.SetTexture(m_ParticleTexture);
    m_Editor.SetParticleSystem(&m_GPUParticleSystem);

    // ==================== 3. PBR Model (Shadowkin) 初期化 ====================
    m_Model = ResourceManager::Get().LoadModel(Res::Mdl::Shadowkin);
    if (m_Model)
    {
        SetupPBRMaterials();
    }
    else
    {
        std::cout << "[Error] Shadowkin load failed" << std::endl;
    }

    // ==================== 4. Skybox 初期化 ====================
    m_Skybox.Init(device, Res::Mdl::SkyboxSphere, Res::Tex::SkyboxPanorama);

    // ==================== 5. Skinned Model 初期化 ====================
    LoadSkinnedModel();

    std::cout << "[TestScene] Init complete" << std::endl;
}

// PBRマテリアル設定（Shadowkin：フルPBRテクスチャ）
void TestScene::SetupPBRMaterials()
{
    auto pbrVS = ResourceManager::Get().LoadVS(L"PBR_VS", Res::Shd::PBR_VS);
    auto pbrPS = ResourceManager::Get().LoadPS(L"PBR_PS", Res::Shd::PBR_PS);

    if (!pbrVS) std::cout << "[Error] PBR_VS failed\n";
    if (!pbrPS) std::cout << "[Error] PBR_PS failed\n";

    auto makePBR = [&](const wchar_t* alb, const wchar_t* nrm, const wchar_t* met,
        const wchar_t* rgh, const wchar_t* ao)
        {
            auto m = std::make_shared<Material>();
            m->SetVertexShader(pbrVS);
            m->SetPixelShader(pbrPS);

            auto albTex = ResourceManager::Get().LoadTexture(alb);
            auto nrmTex = ResourceManager::Get().LoadTexture(nrm);
            auto metTex = ResourceManager::Get().LoadTexture(met);
            auto rghTex = ResourceManager::Get().LoadTexture(rgh);
            auto aoTex = ResourceManager::Get().LoadTexture(ao);

            std::wcout << L"[PBR-tex] alb=" << (albTex ? L"OK" : L"--")
                << L" nrm=" << (nrmTex ? L"OK" : L"--")
                << L" met=" << (metTex ? L"OK" : L"--")
                << L" rgh=" << (rghTex ? L"OK" : L"--")
                << L" ao=" << (aoTex ? L"OK" : L"--") << std::endl;

            m->SetAlbedoTexture(albTex);
            m->SetNormalTexture(nrmTex);
            m->SetMetallicTexture(metTex);
            m->SetRoughnessTexture(rghTex);
            m->SetAOTexture(aoTex);
            return m;
        };

    // Silver（金属）材质
    auto silverMat = makePBR(
        Res::Tex::Silver_Albedo, Res::Tex::Silver_Normal,
        Res::Tex::Silver_Metallic, Res::Tex::Silver_Roughness, Res::Tex::Silver_AO);

    // Pants（布）材质
    auto pantsMat = makePBR(
        Res::Tex::Pants_Albedo, Res::Tex::Pants_Normal,
        Res::Tex::Pants_Metallic, Res::Tex::Pants_Roughness, Res::Tex::Pants_AO);

    int matCount = (int)m_Model->GetMaterialCount();
    std::cout << "[Shadowkin] material count = " << matCount << "\n";

    if (matCount >= 1) m_Model->SetMaterial(0, silverMat);
    if (matCount >= 2) m_Model->SetMaterial(1, pantsMat);
    for (int i = 2; i < matCount; ++i)
        m_Model->SetMaterial(i, silverMat);
}

// スキニングモデル読み込み
void TestScene::LoadSkinnedModel()
{
    auto loaded = ResourceManager::Get().LoadModelAuto(Res::Mdl::Jiandu_Idle);

    if (loaded.kind == ModelKind::Skinned && loaded.skinnedModel)
    {
        m_SkinnedModel = loaded.skinnedModel;

        if (m_SkinnedGPU.Initialize(Application::Get().GetGraphics().GetContext(),
            Application::Get().GetGraphics().GetDevice(),
            *m_SkinnedModel))
        {
            std::cout << "[TestScene] SkinnedModelGPU init OK" << std::endl;
        }
        else
        {
            std::cout << "[TestScene] SkinnedModelGPU init FAILED" << std::endl;
        }
    }
    else
    {
        std::cout << "[TestScene] LoadModelAuto: skinnedとして読めなかった" << std::endl;
    }

    m_SkinningCS = ResourceManager::Get().LoadCS(L"SkinningCS", L"Shader/Skinning/SkinningCS.hlsl");
    m_SkinnedVS = ResourceManager::Get().LoadVS(L"SkinnedVS", L"Shader/Skinning/SkinnedVS.hlsl");
    m_SkinnedPS = ResourceManager::Get().LoadPS(L"SkinnedPS", L"Shader/Skinning/SkinnedPS.hlsl");

    m_SkinnedTransform.SetScale({ 0.005f, 0.005f, 0.005f });
    m_SkinnedTransform.SetPosition({ 0.0f, 0.0f, 0.0f });
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
    m_AnimTime += dt;
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

    RenderSkinnedModel(renderer);

    if (m_Model)
        m_Model->Draw(renderer, &m_ModelTransform);

    m_GPUParticleSystem.SetCamera(GetCamera());
    m_GPUParticleSystem.Render();
}

// スキニングモデル専用描画関数
void TestScene::RenderSkinnedModel(Renderer& renderer)
{
    if (!m_SkinnedModel || !m_SkinningCS || !m_SkinnedVS || !m_SkinnedPS)
        return;

    auto* ctx = Application::Get().GetGraphics().GetContext();

    std::vector<DirectX::SimpleMath::Matrix> globalMatrices;
    std::vector<DirectX::SimpleMath::Matrix> subPalette;

    // Global Transform を1回だけ計算
    m_SkinnedModel->SampleAnimation(m_AnimTime, globalMatrices, 0);

    // ★デバッグ：時間とglobal行列が動いているか（1秒に1回）。修正完了後は削除可
    //static int s_dbg = 0;
    //if (s_dbg++ % 60 == 0 && globalMatrices.size() > 10)
    //{
    //    const auto& m = globalMatrices[10];
    //    std::cout << "[dbg] t=" << m_AnimTime
    //        << " bones=" << globalMatrices.size()
    //        << " g10=(" << m._41 << ", " << m._42 << ", " << m._43 << ")"
    //        << std::endl;
    //}

    // 各SubMeshごとにPaletteを構築してSkinning
    const int subCount = (int)m_SkinnedGPU.GetSubMeshes().size();
    for (int s = 0; s < subCount; ++s)
    {
        m_SkinnedModel->BuildSubmeshPalette(s, globalMatrices, subPalette);
        m_SkinnedGPU.SkinSubmesh(ctx, m_SkinningCS.get(), s, subPalette);
    }

    // 最終描画
    m_SkinnedGPU.Render(ctx, m_SkinnedVS.get(), m_SkinnedPS.get(),
        m_SkinnedTransform.GetWorldMatrix(),
        GetCamera()->GetViewMatrix(),
        GetCamera()->GetProjectionMatrix());
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

void TestScene::InspectModel(const std::string& filepath)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate);

    if (!scene || !scene->mRootNode)
    {
        std::cout << "[Inspect] 読み込み失敗: " << importer.GetErrorString() << std::endl;
        return;
    }
}