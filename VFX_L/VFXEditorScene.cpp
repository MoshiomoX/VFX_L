// ============================================================
// VFXEditorScene.cpp
// ============================================================
#include "VFXEditorScene.h"
#include "Application.h"
#include "ResourceManager.h"
#include "ResourcePaths.h"
#include "DebugManager.h"
#include "Renderer.h"
#include "imgui.h"
#include <iostream>

// ============================================================
// Init
// ============================================================
void VFXEditorScene::Init()
{
    std::cout << "[VFXEditorScene] Init" << std::endl;

    auto* device = Application::Get().GetGraphics().GetDevice();
    auto* context = Application::Get().GetGraphics().GetContext();

    // ---------- Camera ----------
    // 少し引いた位置から原点を見る（VFX は原点付近で作る想定）
    m_Camera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 10000.0f);
    m_Camera.SetPosition({ 0.0f, 3.0f, -10.0f });
    m_Camera.SetTarget({ 0.0f, 1.0f, 0.0f });
    SetCamera(&m_Camera);

    // ---------- Particle System ----------
    if (!m_ParticleSystem.Initialize(device, context, 100000))
    {
        std::cout << "[Error] ParticleSystem init failed" << std::endl;
        return;
    }
    m_ParticleSystem.SetCamera(&m_Camera);

    m_ParticleTexture = ResourceManager::Get().LoadTexture(Res::Tex::ParticleSheet);
    if (m_ParticleTexture)
        m_ParticleSystem.SetTexture(m_ParticleTexture);

    // ---------- VFX ----------
    m_VFXContext.particleSystem = &m_ParticleSystem;
    m_Effect.InitStateMachine(m_VFXContext);

    m_Editor.SetEffect(&m_Effect);
    m_Editor.SetContext(m_VFXContext);
    m_Editor.SetTexture(m_ParticleTexture);
    m_Editor.SetParticleSystem(&m_ParticleSystem);

    std::cout << "[VFXEditorScene] Init complete" << std::endl;
}

// ============================================================
// Shutdown
// ============================================================
void VFXEditorScene::Shutdown()
{
    m_Effect.Stop();
    std::cout << "[VFXEditorScene] Shutdown" << std::endl;
}

// ============================================================
// Update
// ============================================================
void VFXEditorScene::Update(float dt)
{
    SceneBase::Update(dt);
    m_TotalTime += dt;

    // ---- 仮想投射物の移動（追従の確認用）----
    UpdateFakeProjectile(dt);

    // ---- VFX のワールドオフセットを決める ----
    // 追従ON  : 仮想投射物の位置を使う
    // 追従OFF : ImGui の手動オフセットを使う
    if (m_FakeProjectileOn)
        m_Effect.SetWorldOffset(m_FakePos);
    else
        m_Effect.SetWorldOffset({ m_ManualOffset[0], m_ManualOffset[1], m_ManualOffset[2] });

    // ---- VFX 更新（ここで emitter が積まれる）----
    m_Effect.Update(dt);

    // ---- 統計を退避（Flush でクリアされる）----
    m_LastEmitterCount = m_ParticleSystem.GetPendingEmitterCount();
    m_LastDropped = m_ParticleSystem.GetDroppedEmitterCount();

    // ---- ★1フレーム1回だけ ----
    m_ParticleSystem.Flush(dt, m_TotalTime);

    // ---- 仮想投射物の位置を可視化 ----
    if (m_ShowFakeMarker)
    {
        auto& dbg = DebugManager::Get();
        Vector3 p = m_FakeProjectileOn
            ? m_FakePos
            : Vector3(m_ManualOffset[0], m_ManualOffset[1], m_ManualOffset[2]);

        // VFX の基準点を十字で表示（emitter の position はここからの相対）
        const float m = 0.4f;
        Color col(0.3f, 1.0f, 0.5f, 1.0f);
        dbg.AddDebugLine(p - Vector3(m, 0, 0), p + Vector3(m, 0, 0), col);
        dbg.AddDebugLine(p - Vector3(0, m, 0), p + Vector3(0, m, 0), col);
        dbg.AddDebugLine(p - Vector3(0, 0, m), p + Vector3(0, 0, m), col);

        // 追従ON なら進行経路も描く
        if (m_FakeProjectileOn)
        {
            Vector3 dir = m_FakeDir;
            dir.Normalize();
            dbg.AddDebugLine(m_FakeStart, m_FakeStart + dir * m_FakeRange,
                Color(0.4f, 0.4f, 0.5f, 1.0f));
        }
    }

    // ---- UI ----
    DrawSceneUI();
    m_Editor.Draw();
}

// ============================================================
// Render
// ============================================================
void VFXEditorScene::Render(Renderer& renderer)
{
    renderer.SetDirectionalLight(
        { m_LightDir[0], m_LightDir[1], m_LightDir[2] },
        { 1.0f, 1.0f, 1.0f }, 1.0f);
    renderer.SetAmbientColor({ m_AmbientColor[0], m_AmbientColor[1], m_AmbientColor[2] });

    SceneBase::Render(renderer);

    m_ParticleSystem.SetCamera(GetCamera());
    m_ParticleSystem.Render();
}

// ============================================================
// 仮想投射物：一定方向へ進み、範囲を超えたら最初へ戻る
//   実際の投射物と同じ「動きながら発射し続ける」状況を再現し、
//   追従のズレや尾の見え方を確認するためのもの。
// ============================================================
void VFXEditorScene::UpdateFakeProjectile(float dt)
{
    if (!m_FakeProjectileOn)
    {
        m_FakeTravel = 0.0f;
        m_FakePos = m_FakeStart;
        return;
    }

    Vector3 dir = m_FakeDir;
    if (dir.LengthSquared() < 1e-6f) dir = { 1, 0, 0 };
    dir.Normalize();

    m_FakeTravel += m_FakeSpeed * dt;
    if (m_FakeTravel > m_FakeRange)
        m_FakeTravel = 0.0f;   // ループさせて繰り返し確認できるようにする

    m_FakePos = m_FakeStart + dir * m_FakeTravel;
}

// ============================================================
// シーン側 UI（VFXEditor 本体とは別のパネル）
// ============================================================
void VFXEditorScene::DrawSceneUI()
{
    ImGui::Begin("VFX Scene");

    // ---------- 統計 ----------
    ImGui::Text("Emitters : %zu / %zu", m_LastEmitterCount, m_ParticleSystem.GetMaxEmitters());
    if (m_LastDropped > 0)
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Dropped : %zu", m_LastDropped);
    ImGui::Text("Alive Particles : %u", m_ParticleSystem.GetAliveCount());
    ImGui::Separator();

    // ---------- 投射物追従テスト ----------
    if (ImGui::CollapsingHeader("Projectile Follow Test", ImGuiTreeNodeFlags_DefaultOpen))
    {
      
        ImGui::Checkbox("Enable Follow", &m_FakeProjectileOn);
        ImGui::SameLine();
        ImGui::Checkbox("Show Marker", &m_ShowFakeMarker);

        if (m_FakeProjectileOn)
        {
            ImGui::DragFloat3("Start", &m_FakeStart.x, 0.1f);
            ImGui::DragFloat3("Dir", &m_FakeDir.x, 0.02f, -1.0f, 1.0f);
            ImGui::DragFloat("Speed", &m_FakeSpeed, 0.5f, 0.0f, 100.0f);
            ImGui::DragFloat("Range", &m_FakeRange, 0.5f, 1.0f, 100.0f);
            ImGui::Text("Pos : %.2f, %.2f, %.2f", m_FakePos.x, m_FakePos.y, m_FakePos.z);
            ImGui::ProgressBar(m_FakeTravel / m_FakeRange, ImVec2(-1, 0), "travel");
        }
        else
        {
            // 追従OFF：手動で基準点を動かす（静止した状態で形を確認）
            ImGui::DragFloat3("Manual Offset", m_ManualOffset, 0.05f);
            if (ImGui::Button("Reset Offset"))
            {
                m_ManualOffset[0] = 0.0f;
                m_ManualOffset[1] = 0.0f;
                m_ManualOffset[2] = 0.0f;
            }
        }
    }

    // ---------- 再生制御 ----------
    if (ImGui::CollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("Play"))  m_Effect.Play();
        ImGui::SameLine();
        if (ImGui::Button("Stop"))  m_Effect.Stop();
        ImGui::SameLine();
        if (ImGui::Button("Reset Particles")) m_ParticleSystem.ResetSystem();
    }

    // ---------- 環境 ----------
    if (ImGui::CollapsingHeader("Environment"))
    {
        ImGui::DragFloat3("Light Dir", m_LightDir, 0.02f, -1.0f, 1.0f);
        ImGui::ColorEdit3("Ambient", m_AmbientColor);
        ImGui::Separator();
        ImGui::DragFloat3("Camera Pos", (float*)&m_Camera, 0.0f);   // 表示のみ
    }

   

    ImGui::End();
}