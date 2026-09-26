// ============================================================
// StressTestTools.cpp
// ============================================================
#include "Debug/StressTestTools.h"
#include "Swarm/SwarmSystem.h"
#include "Particle/GPUParticleSystem.h"
#include "Collider/CollisionSystem.h"
#include "Graphics/Renderer/ProjectileBillboardRenderer.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Mesh/Mesh.h"
#include "Component/TransformComponent.h"
#include "Component/ModelComponent.h"
#include "VFX_Editor/VFXId.h"
#include "imgui.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace DirectX::SimpleMath;

// ============================================================
// 補充（枯渇状態を維持し続けるための自動生成）と小分けの生成
// 上限は SwarmSystem::SpawnProjectile の1フレーム受付数。超えた分は静かに捨てられる
// ============================================================
void StressTestTools::Update(float dt, Registry& reg, Entity player, SwarmSystem& swarm)
{
    if (m_AutoRefill)
    {
        m_RefillTimer += dt;
        if (m_RefillTimer >= m_RefillInterval)
        {
            m_RefillTimer = 0.0f;
            if ((int)swarm.GetCounters().aliveProjectiles + m_StressPending < m_RefillTarget)
                m_StressPending += m_RefillBatch;
        }
    }

    if (m_StressPending > 0)
    {
        const int maxPerFrame = (int)Swarm::kMaxSpawnProjPerFrame;
        int batch = (m_StressPending < maxPerFrame) ? m_StressPending : maxPerFrame;
        SpawnProjectiles(reg, player, swarm, batch);
        m_StressPending -= batch;
    }
}

// ============================================================
// 投射物をばら撒く
// 生成先は GPU（SwarmSystem）。弾1つにつき emitter が1つ積まれるので、
// 粒子側の経路（SwarmEmitCS / deadList）に負荷がかかる。
// 1フレームの受付上限は kMaxSpawnProjPerFrame。呼び出し側が小分けにする
// ============================================================
void StressTestTools::SpawnProjectiles(Registry& reg, Entity player, SwarmSystem& swarm, int count)
{
    if (!reg.IsValid(player)) return;
    Vector3 origin = reg.Get<TransformComponent>(player).position + Vector3(0, 2.0f, 0);

    for (int i = 0; i < count; ++i)
    {
        float a = (float)rand() / RAND_MAX * 6.2831853f;
        float b = (float)rand() / RAND_MAX * 6.2831853f;
        Vector3 dir(std::cos(a) * std::cos(b), std::sin(b) * 0.3f, std::sin(a) * std::cos(b));
        dir.Normalize();

        swarm.SpawnProjectile(VFXId::Fireball, origin, dir * 8.0f, 1.0f, 0.25f, 30.0f);
    }
}

// ============================================================
// 既定値セットを適用する
// ============================================================
void StressTestTools::ApplyPreset(const StressPreset& p, SwarmSystem& swarm)
{
    swarm.ClearProjectiles();
    m_StressPending = 0;

    m_RefillTarget = p.target;
    m_RefillBatch = p.batch;
    m_AutoRefill = p.autoRefill;

    m_FlushMsPeak = 0.0;
    m_FlushMsAvg = 0.0;
    m_RefillTimer = 0.0f;

    std::cout << "[Stress] preset applied: " << p.name
        << "  (" << p.purpose << ")" << std::endl;
}

void StressTestTools::RecordFlushMs(double ms)
{
    m_FlushMs = ms;
    m_FlushMsAvg = m_FlushMsAvg * 0.95 + ms * 0.05;
    if (ms > m_FlushMsPeak) m_FlushMsPeak = ms;
}

// ============================================================
// Mesh 発射の動作確認（仮設）
// 玩家の胶囊 Mesh（VERTEX_3D）を発射源に登録し、
// TransformComponent から作った世界行列を毎フレーム emitter に渡す。
// 期待：粒子が胶囊の表面から法線方向に出て、玩家が向きを変えると付いて回る
// ============================================================
void StressTestTools::UpdateMeshEmitTest(float dt, Registry& reg, Entity player, GPUParticleSystem& particles)
{
    if (!m_MeshEmitTest)
    {
        if (m_MeshEmitSourceId >= 0)
        {
            particles.UnregisterEmitSource(m_MeshEmitSourceId);
            m_MeshEmitSourceId = -1;
            m_MeshEmitModel.reset();
        }
        return;
    }

    if (!reg.IsValid(player) || !reg.Has<ModelComponent>(player))
        return;

    auto model = reg.Get<ModelComponent>(player).model;
    if (!model || model->GetSubMeshes().empty() || !model->GetSubMeshes()[0].mesh)
        return;

    // RebuildVisual でモデルが差し替わったら登録し直す
    if (model != m_MeshEmitModel)
    {
        if (m_MeshEmitSourceId >= 0)
            particles.UnregisterEmitSource(m_MeshEmitSourceId);

        const auto& mesh = model->GetSubMeshes()[0].mesh;
        m_MeshEmitSourceId = particles.RegisterEmitSource(
            mesh->GetVertexSRV(), mesh->GetVertexCount(), GPUParticleSystem::kLayoutStatic,
            mesh->GetIndexSRV(), mesh->GetIndexCount(), mesh->GetIndexBytes());
        m_MeshEmitModel = model;

        m_MeshEmitter.emitType = EmitType::Mesh;
        m_MeshEmitter.shape.sourceId = m_MeshEmitSourceId;
        m_MeshEmitter.shape.sourceCount = (int)mesh->GetVertexCount();
        m_MeshEmitter.shape.edgeMode = 0;
        m_MeshEmitter.position = { 0, 0, 0 };
        m_MeshEmitter.speedRange = { 0.3f, 1.0f };
        m_MeshEmitter.lifetimeRange = { 0.4f, 0.8f };
        m_MeshEmitter.sizeRange = { 0.06f, 0.10f, 0.0f, 0.02f };
        m_MeshEmitter.startColorMin = { 1.0f, 0.6f, 0.2f, 1.0f };
        m_MeshEmitter.startColorMax = { 1.0f, 0.9f, 0.4f, 1.0f };
        m_MeshEmitter.endColorMin = { 1.0f, 0.2f, 0.0f, 0.0f };
        m_MeshEmitter.endColorMax = { 1.0f, 0.4f, 0.0f, 0.0f };
        m_MeshEmitter.gravity = { 0, 0.5f, 0 };
        m_MeshEmitter.dragCoeff = 0.5f;
        m_MeshEmitter.atlasRows = 1;
        m_MeshEmitter.atlasCols = 1;
        m_MeshEmitter.atlasIndex = 0;
        m_MeshEmitter.colorKeyCount = 0;

        std::cout << "[MeshEmitTest] source id=" << m_MeshEmitSourceId
            << " verts=" << mesh->GetVertexCount() << std::endl;
    }

    if (m_MeshEmitSourceId < 0) return;

    // Transform::UpdateWorldMatrix と同じ式（scale * rot(yaw=y, pitch=x, roll=z) * trans）
    const auto& tf = reg.Get<TransformComponent>(player);
    m_MeshEmitter.world =
        Matrix::CreateScale(tf.scale) *
        Matrix::CreateFromYawPitchRoll(
            DirectX::XMConvertToRadians(tf.rotation.y),
            DirectX::XMConvertToRadians(tf.rotation.x),
            DirectX::XMConvertToRadians(tf.rotation.z)) *
        Matrix::CreateTranslation(tf.position);

    m_MeshEmitter.emitRate = m_MeshEmitRate;
    m_MeshEmitter.Update(dt);

    std::vector<GPUEmitter> emitters{ m_MeshEmitter.ToGPU() };
    std::vector<ColorKey>   keys;
    particles.SubmitEmitters(emitters, keys);
}

// ============================================================
// ImGui: 負荷テスト
// ============================================================
void StressTestTools::DrawImGui(SwarmSystem& swarm, const CollisionSystem& collision,
    const GPUParticleSystem& particles, const ProjectileBillboardRenderer& billboards)
{
    if (!ImGui::CollapsingHeader("Stress Test"))
        return;

    // ---------- 既定値セット ----------
    // 毎回 slider を合わせ直すと条件がぶれて比較にならない。
    // ボタン1つで同じ条件を再現できるようにする。
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Presets");
    ImGui::TextDisabled("Clears projectiles and resets counters, then applies.");

    const int count = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
    for (int i = 0; i < count; ++i)
    {
        const auto& p = kPresets[i];
        if (i % 2 != 0) ImGui::SameLine();

        const bool active = (m_LastPresetIndex == i);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.9f, 1.0f));

        if (ImGui::Button(p.name, ImVec2(150, 0)))
        {
            ApplyPreset(p, swarm);
            m_LastPresetIndex = i;
        }

        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.purpose);
    }

    if (m_LastPresetIndex >= 0)
        ImGui::TextDisabled("current: %s", kPresets[m_LastPresetIndex].name);

    // ---------- 現在の数 ----------
    ImGui::Separator();

    const int projCount = (int)swarm.GetCounters().aliveProjectiles;   // 回読なので 1〜2 フレーム古い
    ImGui::Text("Projectiles : %d   (pending %d)", projCount, m_StressPending);
    ImGui::Text("Colliders   : %zu", collision.GetWorldColliders().size());
    ImGui::Text("Pairs       : %zu", collision.GetPairs().size());

    // ---------- 生成 ----------
    ImGui::Separator();
    ImGui::SliderInt("Spawn Count", &m_StressCount, 50, 2000);
    if (ImGui::Button("+ Spawn")) m_StressPending += m_StressCount;
    ImGui::SameLine();
    if (ImGui::Button("Clear All"))
    {
        swarm.ClearProjectiles();
        m_StressPending = 0;
    }

    // ---------- プール枯渇テスト ----------
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Pool Starvation Test");
    ImGui::TextDisabled("Keeps the dead list empty. This is the only state");
    ImGui::TextDisabled("where the EmitCS guard actually matters.");

    ImGui::Checkbox("Auto Refill", &m_AutoRefill);
    ImGui::SliderInt("Target Projectiles", &m_RefillTarget, 100, 4000);
    ImGui::SliderInt("Refill Batch", &m_RefillBatch, 10, 500);

    // ---------- 粒子システムの状態 ----------
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Particle System");

    float ratio = (float)m_LastEmitterCount / (float)particles.GetMaxEmitters();
    ImVec4 col = (ratio > 0.9f) ? ImVec4(1, 0.4f, 0.4f, 1)
        : (ratio > 0.7f) ? ImVec4(1, 0.9f, 0.4f, 1)
        : ImVec4(0.4f, 1, 0.4f, 1);
    ImGui::TextColored(col, "Emitters : %zu / %zu",
        m_LastEmitterCount, particles.GetMaxEmitters());

    if (m_LastDropped > 0)
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
            "Dropped : %zu  (some projectiles have no VFX)", m_LastDropped);

    ImGui::Text("Pool Size      : %u", particles.GetMaxParticles());
    ImGui::Text("Billboards     : %u  (1 draw call)", billboards.GetLastDrawCount());

    // ---------- Mesh 発射の動作確認（仮設）----------
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Mesh Emit Test");
    ImGui::TextDisabled("Particles from the player capsule vertices (raw VB source).");
    ImGui::Checkbox("Emit from Player Mesh", &m_MeshEmitTest);
    ImGui::SliderFloat("Mesh Emit Rate", &m_MeshEmitRate, 0.0f, 5000.0f);
    ImGui::Text("source id : %d", m_MeshEmitSourceId);

    // ---------- Flush の CPU 時間 ----------
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Flush CPU Time");
    ImGui::TextDisabled("Flush only queues commands. It should stay near 0");
    ImGui::TextDisabled("even under load. If not, something waits for the GPU.");

    ImVec4 fcol = (m_FlushMsAvg > 1.0) ? ImVec4(1, 0.4f, 0.4f, 1)
        : (m_FlushMsAvg > 0.3) ? ImVec4(1, 0.9f, 0.4f, 1)
        : ImVec4(0.4f, 1, 0.4f, 1);
    ImGui::TextColored(fcol, "now %.4f ms   avg %.4f ms   peak %.4f ms",
        m_FlushMs, m_FlushMsAvg, m_FlushMsPeak);

    if (ImGui::Button("Reset Peak"))
    {
        m_FlushMsPeak = 0.0;
        m_FlushMsAvg = 0.0;
    }
    ImGui::SameLine();
    ImGui::Text("| FPS %.1f", ImGui::GetIO().Framerate);
}
