// ============================================================
// VFXEditorScene.cpp
// 特効編集用のシーン。粒子 + VFX Mesh + 参照用の骨付きモデル
// ============================================================
#include "Scene/VFXEditorScene.h"
#include "Core/Application.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "Debug/DebugManager.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Material/Material.h"
#include "VFX_Editor/VFXParticleEntry.h"
#include "imgui.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <cstddef>

// kLayoutSkinned が SkinnedVertexOut（SkinningCS の出力）の実際の並びと一致していることの保証
static_assert(sizeof(SkinnedVertexOut) == GPUParticleSystem::kLayoutSkinned.stride, "kLayoutSkinned.stride != sizeof(SkinnedVertexOut)");
static_assert(offsetof(SkinnedVertexOut, position) == GPUParticleSystem::kLayoutSkinned.posOffset, "kLayoutSkinned.posOffset");
static_assert(offsetof(SkinnedVertexOut, normal) == GPUParticleSystem::kLayoutSkinned.normalOffset, "kLayoutSkinned.normalOffset");
static_assert(offsetof(SkinnedVertexOut, uv) == GPUParticleSystem::kLayoutSkinned.uvOffset, "kLayoutSkinned.uvOffset");

using namespace DirectX::SimpleMath;

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

    // ---------- VFX Mesh Renderer ----------
    // Mesh entry の描画先。粒子と同じく実行時の物で、Editor は指針だけ持つ
    if (!m_MeshRenderer.Initialize(device))
        std::cout << "[Error] VFXMeshRenderer init failed" << std::endl;

    // ---------- VFX（context は値渡しなので、指針を全部埋めてから渡す）----------
    m_VFXContext.particleSystem = &m_ParticleSystem;
    m_VFXContext.meshRenderer = &m_MeshRenderer;
    m_Effect.InitStateMachine(m_VFXContext);

    m_Editor.SetEffect(&m_Effect);
    m_Editor.SetContext(m_VFXContext);
    m_Editor.SetTexture(m_ParticleTexture);
    m_Editor.SetParticleSystem(&m_ParticleSystem);
    m_Editor.SetMeshRenderer(&m_MeshRenderer);

    // ---------- 参照用モデル（Paladin, 骨付き）----------
    // 粒子だけだと大きさと明るさの基準が無い。動く物を置く
    Material::InitDefaultTextures(device);
    auto loaded = ResourceManager::Get().LoadModelAuto(Res::Mdl::Paladin_Idle);
    if (loaded.kind == ModelKind::Skinned && loaded.skinnedModel)
    {
        m_SkinnedModel = loaded.skinnedModel;
        if (!m_SkinnedGPU.Initialize(context, device, *m_SkinnedModel))
            std::cout << "[Error] SkinnedModelGPU init failed" << std::endl;
    }
    else
        std::cout << "[Error] Paladin: not a skinned model" << std::endl;

    m_SkinningCS = ResourceManager::Get().LoadCS(L"SkinningCS", L"Shader/Skinning/SkinningCS.hlsl");

    // 参照モデルの submesh を粒子の発射源として登録し、Inspector の「Source」候補に出す。
    // ※m_Editor.SetContext は値コピーなので、登録後にもう一度渡す
    RegisterRefSources();
    m_VFXContext.refSources = &m_RefSources;
    m_VFXContext.refWorld = &m_ModelWorld;
    m_Editor.SetContext(m_VFXContext);

    // 一括発射の既定値をプール全体に合わせる
    m_BurstCount = (int)m_ParticleSystem.GetMaxParticles();

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

    // ---- 参照モデル ----
    m_ModelTransform.SetPosition({ m_ModelPos[0], m_ModelPos[1], m_ModelPos[2] });
    m_ModelTransform.SetRotation({ m_ModelRot[0], m_ModelRot[1], m_ModelRot[2] });
    m_ModelTransform.SetScale({ m_ModelScale[0], m_ModelScale[1], m_ModelScale[2] });
    m_ModelWorld = m_ModelTransform.GetWorldMatrix();   // Mesh 発射の followWorld 用
    if (m_AnimPlay) m_AnimTime += dt * m_AnimSpeed;

    // ---- VFX 更新（ここで emitter と mesh item が積まれる）----
    m_Effect.Update(dt);

    // ============================================================
    // 一括発射は VFX の後、Flush の前に積む。
    // 同じフレームの emitter 配列に合流させるため。
    // ============================================================
    if (m_BurstPending || m_BurstLoop)
    {
        SubmitBurst();
        m_BurstPending = false;
    }
    else
    {
        m_LastBurstRequest = 0;
    }

    // ---- 統計を退避（Flush でクリアされる）----
    m_LastEmitterCount = m_ParticleSystem.GetPendingEmitterCount();
    m_LastDropped = m_ParticleSystem.GetDroppedEmitterCount();

    // ============================================================
    // 1フレーム1回だけ。Flush はコマンドを積むだけの処理
    // ============================================================
    {
        auto t0 = std::chrono::high_resolution_clock::now();

        m_ParticleSystem.Flush(dt, m_TotalTime);

        auto t1 = std::chrono::high_resolution_clock::now();
        m_FlushMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        m_FlushMsAvg = m_FlushMsAvg * 0.95 + m_FlushMs * 0.05;
        if (m_FlushMs > m_FlushMsPeak) m_FlushMsPeak = m_FlushMs;
    }

    // ---- 仮想投射物の位置を可視化 ----
    if (m_ShowFakeMarker)
    {
        auto& dbg = DebugManager::Get();
        Vector3 p = m_FakeProjectileOn
            ? m_FakePos
            : Vector3(m_ManualOffset[0], m_ManualOffset[1], m_ManualOffset[2]);

        const float m = 0.4f;
        Color col(0.3f, 1.0f, 0.5f, 1.0f);
        dbg.AddDebugLine(p - Vector3(m, 0, 0), p + Vector3(m, 0, 0), col);
        dbg.AddDebugLine(p - Vector3(0, m, 0), p + Vector3(0, m, 0), col);
        dbg.AddDebugLine(p - Vector3(0, 0, m), p + Vector3(0, 0, m), col);

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
// 不透明（参照モデル）→ VFX Mesh → 粒子 の順。後ろ2つは深度を読むだけ
// ============================================================
void VFXEditorScene::Render(Renderer& renderer)
{
    renderer.SetDirectionalLight(
        { m_LightDir[0], m_LightDir[1], m_LightDir[2] },
        { 1.0f, 1.0f, 1.0f }, m_LightIntensity);
    renderer.SetAmbientColor({ m_AmbientColor[0], m_AmbientColor[1], m_AmbientColor[2] });

    SceneBase::Render(renderer);

    auto* ctx = Application::Get().GetGraphics().GetContext();

    // ---- 参照モデル（骨付き）----
    if (m_ShowModel && m_SkinnedModel && m_SkinningCS)
    {
        std::vector<Matrix> globals, palette;
        m_SkinnedModel->SampleAnimation(m_AnimTime, globals, 0);

        const int subCount = (int)m_SkinnedGPU.GetSubMeshes().size();
        for (int s = 0; s < subCount; ++s)
        {
            m_SkinnedModel->BuildSubmeshPalette(s, globals, palette);
            m_SkinnedGPU.SkinSubmesh(ctx, m_SkinningCS.get(), s, palette);
        }

        LightBuffer l = renderer.GetLightData();
        l.cameraPosition = GetCamera()->GetPosition();
        m_SkinnedGPU.Render(ctx, *m_SkinnedModel, l,
            m_ModelTransform.GetWorldMatrix(),
            GetCamera()->GetViewMatrix(),
            GetCamera()->GetProjectionMatrix());

        RenderStates::Get().Restore(ctx);
    }

    // ---- VFX Mesh（光を当てない。深度は読むだけ）----
    m_MeshRenderer.Render(ctx, GetCamera());

    // ---- 粒子 ----
    m_ParticleSystem.SetCamera(GetCamera());
    m_ParticleSystem.SetLight(renderer.GetLightData());   // 立方体粒子の Lambert 用
    m_ParticleSystem.Render();
}

// ============================================================
// 参照モデルの submesh を、選択中の Particle entry の Mesh 発射源にする
// 源の登録は submesh ごとに 1 回。世界行列は m_ModelWorld を毎フレーム追う
// ============================================================
void VFXEditorScene::RegisterRefSources()
{
    m_RefSources.clear();
    if (!m_SkinnedModel) return;

    const auto& subs = m_SkinnedModel->GetSubMeshes();
    const int count = (int)m_SkinnedGPU.GetSubMeshes().size();
    for (int i = 0; i < count; ++i)
    {
        VFXRefEmitSource r;
        r.name = (i < (int)subs.size()) ? subs[i].name : ("submesh " + std::to_string(i));
        r.vertexCount = (int)m_SkinnedGPU.GetSubMeshVertexCount(i);
        r.sourceId = m_ParticleSystem.RegisterEmitSource(
            m_SkinnedGPU.GetSkinnedRawSRV(i), m_SkinnedGPU.GetSubMeshVertexCount(i),
            GPUParticleSystem::kLayoutSkinned,
            m_SkinnedGPU.GetIndexRawSRV(i), m_SkinnedGPU.GetSubMeshIndexCount(i), 4);
        if (r.sourceId < 0)
        {
            std::cout << "[VFXEditorScene] RegisterEmitSource failed for submesh " << i << std::endl;
            continue;
        }
        // 毎フレームの姿勢を raw 双子へ写す（48〜64B × 頂点数の複写、軽い）
        m_SkinnedGPU.SetEmitSourceEnabled(i, true);
        m_RefSources.push_back(r);
    }
    std::cout << "[VFXEditorScene] ref emit sources: " << m_RefSources.size() << std::endl;
}

void VFXEditorScene::UseRefModelAsSource(int submesh)
{
    const VFXRefEmitSource* src = nullptr;
    for (const auto& r : m_RefSources)
        if (r.name == m_SkinnedModel->GetSubMeshes()[submesh].name) { src = &r; break; }
    if (!src)
    {
        std::cout << "[VFXEditorScene] submesh " << submesh << " is not registered as a source" << std::endl;
        return;
    }

    auto* entry = m_Effect.GetEntry(m_Editor.GetSelectedEntry());
    if (!entry || entry->GetType() != EntryType::Particle)
    {
        std::cout << "[VFXEditorScene] select a Particle entry first" << std::endl;
        return;
    }
    static_cast<VFXParticleEntry*>(entry)->SetExternalSource(src->sourceId, src->vertexCount, &m_ModelWorld);
}

// ============================================================
// 仮想投射物：一定方向へ進み、範囲を超えたら最初へ戻る
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
    {
        m_FakeTravel = 0.0f;
        m_Effect.NotifyTeleport();   // 終点 → 始点の瞬間移動。粒子の線で結ばない
    }

    m_FakePos = m_FakeStart + dir * m_FakeTravel;
}

// ============================================================
// 一括発射：emitter を1つだけ積んで、そこに全弾を担当させる
// CPU は「何発撃ちたいか」しか書かない。実際の数は EmitCS が決める
// ============================================================
void VFXEditorScene::SubmitBurst()
{
    GPUEmitter e = {};

    e.position = { m_BurstOrigin[0], m_BurstOrigin[1], m_BurstOrigin[2] };
    e.emitType = 1;                       // Sphere
    e.direction = { 0.0f, 1.0f, 0.0f };
    e.spreadAngle = 180.0f;
    e.shapeSize = { 0.5f, 0.5f, 0.5f };

    e.emitCount = m_BurstCount;
    e.maxParticles = (int)m_ParticleSystem.GetMaxParticles();
    e.particleOffset = 0;
    e.emitRate = 0.0f;

    e.speedRange = { m_BurstSpeed[0], m_BurstSpeed[1] };
    e.lifetimeRange = { m_BurstLife[0], m_BurstLife[1] };
    e.sizeRange = { m_BurstSize[0], m_BurstSize[0], m_BurstSize[1], m_BurstSize[1] };

    e.startColorMin = { 0.6f, 0.8f, 1.0f, 1.0f };
    e.startColorMax = { 1.0f, 1.0f, 1.0f, 1.0f };
    e.endColorMin = { 0.2f, 0.1f, 0.6f, 0.0f };
    e.endColorMax = { 0.6f, 0.2f, 1.0f, 0.0f };

    e.gravity = { 0.0f, m_BurstGravity, 0.0f };
    e.dragCoeff = m_BurstDrag;

    e.rotationRange = { 0.0f, 360.0f };
    e.angularVelRange = { -90.0f, 90.0f };

    e.sourceId = -1;
    e.sourceCount = 0;

    e.isActive = 1.0f;
    e.emitterID = 9999;

    e.atlasRows = 1;
    e.atlasCols = 1;
    e.atlasIndex = 0;
    e.textureIndex = 0;

    e.colorKeyOffset = 0;
    e.colorKeyCount = 0;

    std::vector<GPUEmitter> emitters{ e };
    std::vector<ColorKey>   keys;

    m_ParticleSystem.SubmitEmitters(emitters, keys);
    m_LastBurstRequest = (size_t)m_BurstCount;
}

// ============================================================
// 負荷テストのパネル
// ============================================================
void VFXEditorScene::DrawStressUI()
{
    if (!ImGui::CollapsingHeader("Burst Test"))
        return;

    const uint32_t pool = m_ParticleSystem.GetMaxParticles();

    ImGui::Text("Pool Size : %u", pool);
    ImGui::TextDisabled("Fills the whole pool in a single frame.");
    ImGui::TextDisabled("If the request exceeds the free slots, the EmitCS");
    ImGui::TextDisabled("guard stops the extra threads. Nothing breaks.");
    ImGui::Separator();

    ImGui::SliderInt("Burst Count", &m_BurstCount, 1000, (int)pool);
    if (ImGui::Button("Full Pool")) m_BurstCount = (int)pool;
    ImGui::SameLine();
    if (ImGui::Button("Half"))      m_BurstCount = (int)pool / 2;
    ImGui::SameLine();
    if (ImGui::Button("Over x2"))   m_BurstCount = (int)pool * 2;

    ImGui::Spacing();
    if (ImGui::Button("BURST", ImVec2(160, 40)))
        m_BurstPending = true;
    ImGui::SameLine();
    ImGui::Checkbox("Loop (keep starving)", &m_BurstLoop);
    if (m_BurstLoop)
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
            "Requesting %d every frame -> dead list stays empty", m_BurstCount);
    ImGui::Separator();

    ImGui::DragFloat3("Origin", m_BurstOrigin, 0.1f);
    ImGui::DragFloat2("Speed  min/max", m_BurstSpeed, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat2("Life   min/max", m_BurstLife, 0.1f, 0.1f, 30.0f);
    ImGui::DragFloat2("Size   start/end", m_BurstSize, 0.005f, 0.001f, 2.0f);
    ImGui::DragFloat("Gravity", &m_BurstGravity, 0.1f, -30.0f, 30.0f);
    ImGui::DragFloat("Drag", &m_BurstDrag, 0.02f, 0.0f, 5.0f);
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Result");
    ImGui::Text("Requested this frame : %zu", m_LastBurstRequest);
    ImGui::Text("Emitters submitted   : %zu / %zu",
        m_LastEmitterCount, m_ParticleSystem.GetMaxEmitters());
    ImGui::TextDisabled("How many actually spawned lives on the GPU only.");
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Flush CPU Time");
    ImVec4 c = (m_FlushMsAvg > 1.0) ? ImVec4(1, 0.4f, 0.4f, 1)
        : (m_FlushMsAvg > 0.3) ? ImVec4(1, 0.9f, 0.4f, 1)
        : ImVec4(0.4f, 1, 0.4f, 1);
    ImGui::TextColored(c, "now %.4f ms   avg %.4f ms   peak %.4f ms",
        m_FlushMs, m_FlushMsAvg, m_FlushMsPeak);
    if (ImGui::Button("Reset Peak"))
    {
        m_FlushMsPeak = 0.0;
        m_FlushMsAvg = 0.0;
    }
    ImGui::SameLine();
    ImGui::Text("| FPS %.1f", ImGui::GetIO().Framerate);
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
    ImGui::TextDisabled("Alive count lives on the GPU only.");
    ImGui::Separator();

    // ---------- 投射物追従テスト ----------
    if (ImGui::CollapsingHeader("Projectile Follow Test"))
    {
        // 切り替えた瞬間は位置が飛ぶので、前の位置と繋がない
        if (ImGui::Checkbox("Enable Follow", &m_FakeProjectileOn))
            m_Effect.NotifyTeleport();
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

            // 掃引発射の ON/OFF 見比べ。OFF だと Speed を上げた時に軌跡が点々になる
            bool sweepOn = m_Effect.IsSweepEnabled();
            if (ImGui::Checkbox("Sweep Emit (fill trail gaps)", &sweepOn))
                m_Effect.SetSweepEnabled(sweepOn);
            ImGui::TextDisabled("gap per frame without sweep: %.2f m fps", m_FakeSpeed / 60.0f);
        }
        else
        {
            ImGui::DragFloat3("Manual Offset", m_ManualOffset, 0.05f);
            if (ImGui::Button("Reset Offset"))
                m_ManualOffset[0] = m_ManualOffset[1] = m_ManualOffset[2] = 0.0f;
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

    // ---------- 負荷テスト ----------
    DrawStressUI();

    // ---------- 環境 ----------
    if (ImGui::CollapsingHeader("Environment"))
    {
        ImGui::DragFloat3("Light Dir", m_LightDir, 0.02f, -1.0f, 1.0f);
        ImGui::DragFloat("Light Intensity", &m_LightIntensity, 0.05f, 0.0f, 10.0f);
        ImGui::ColorEdit3("Ambient", m_AmbientColor);
    }

    // ---------- 参照モデル ----------
    if (ImGui::CollapsingHeader("Reference Model"))
    {
        ImGui::Checkbox("Show Model", &m_ShowModel);
        ImGui::DragFloat3("Model Pos", m_ModelPos, 0.05f);
        ImGui::DragFloat3("Model Rot", m_ModelRot, 1.0f);
        ImGui::DragFloat3("Model Scale", m_ModelScale, 0.001f, 0.001f, 10.0f);

        ImGui::Separator();
        ImGui::Checkbox("Anim Play", &m_AnimPlay);
        ImGui::SameLine();
        ImGui::DragFloat("Anim Speed", &m_AnimSpeed, 0.05f, 0.0f, 4.0f);
        if (ImGui::Button("Anim Reset")) m_AnimTime = 0.0f;

        // どのパーツが浮いているか特定する用
        if (m_SkinnedModel && ImGui::TreeNode("SubMeshes"))
        {
            const auto& subs = m_SkinnedModel->GetSubMeshes();
            for (int i = 0; i < (int)subs.size(); ++i)
            {
                ImGui::PushID(i);
                bool v = m_SkinnedGPU.IsSubMeshVisible(i);
                if (ImGui::Checkbox(subs[i].name.c_str(), &v))
                    m_SkinnedGPU.SetSubMeshVisible(i, v);
                ImGui::SameLine();
                ImGui::TextDisabled("(%zu verts)", subs[i].vertices.size());
                ImGui::SameLine();
                // 選択中の Particle entry（Shape=Mesh）をこの submesh から発射させる
                if (ImGui::SmallButton("Use as emit source"))
                    UseRefModelAsSource(i);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }

    // ---------- Bloom（後処理。粒子の見た目に直結するのでここで触る）----------
    if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& bp = Application::Get().GetGraphics().GetBloomParams();
        ImGui::Checkbox("Enabled", &bp.enabled);
        ImGui::DragFloat("Threshold", &bp.threshold, 0.01f, 0.0f, 4.0f);
        ImGui::DragFloat("Knee", &bp.knee, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Intensity", &bp.intensity, 0.01f, 0.0f, 5.0f);
        ImGui::Separator();
        ImGui::DragFloat("Exposure", &bp.exposure, 0.01f, 0.1f, 8.0f);
        ImGui::Checkbox("Tonemap (ACES)", &bp.tonemap);
        ImGui::SameLine();
        ImGui::Checkbox("Gamma", &bp.gamma);
    }

    ImGui::End();
}