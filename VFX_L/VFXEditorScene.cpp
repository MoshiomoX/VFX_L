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
#include <vector>
#include <chrono>

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

    // ---- VFX 更新（ここで emitter が積まれる）----
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
    // 1フレーム1回だけ
    //   Flush はコマンドを積むだけの処理。
    //   10万発を要求しても CPU 時間はほぼ変わらないはず。
    //   変わるなら、どこかで GPU の完了を待っている。
    // ============================================================
    {
        auto t0 = std::chrono::high_resolution_clock::now();

        m_ParticleSystem.Flush(dt, m_TotalTime);

        auto t1 = std::chrono::high_resolution_clock::now();
        m_FlushMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // 指数移動平均。単発の外れ値に振られないようにする。
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
// 一括発射：emitter を1つだけ積んで、そこに全弾を担当させる
//
// CPU は「何発撃ちたいか」しか書かない。
//   実際に何発出るかは EmitCS が dead list の空き数を見て決める。
//   要求が空き数を超えても、shader 側の護欄が余分なスレッドを
//   止めるので dead list は壊れない。
//   これを目で確認するのがこのテストの目的。
// ============================================================
void VFXEditorScene::SubmitBurst()
{
    GPUEmitter e = {};

    e.position = { m_BurstOrigin[0], m_BurstOrigin[1], m_BurstOrigin[2] };
    e.emitType = 1;                       // Sphere（全方向へ均等に散る）
    e.direction = { 0.0f, 1.0f, 0.0f };
    e.spreadAngle = 180.0f;
    e.shapeSize = { 0.5f, 0.5f, 0.5f };

    e.emitCount = m_BurstCount;
    e.maxParticles = (int)m_ParticleSystem.GetMaxParticles();
    e.particleOffset = 0;
    e.emitRate = 0.0f;                    // 連続発射ではないので使わない

    e.speedRange = { m_BurstSpeed[0], m_BurstSpeed[1] };
    e.lifetimeRange = { m_BurstLife[0], m_BurstLife[1] };

    // x=startMin, y=startMax, z=endMin, w=endMax
    e.sizeRange = { m_BurstSize[0], m_BurstSize[0],
                    m_BurstSize[1], m_BurstSize[1] };

    e.startColorMin = { 0.6f, 0.8f, 1.0f, 1.0f };
    e.startColorMax = { 1.0f, 1.0f, 1.0f, 1.0f };
    e.endColorMin = { 0.2f, 0.1f, 0.6f, 0.0f };
    e.endColorMax = { 0.6f, 0.2f, 1.0f, 0.0f };

    e.gravity = { 0.0f, m_BurstGravity, 0.0f };
    e.dragCoeff = m_BurstDrag;

    e.rotationRange = { 0.0f, 360.0f };
    e.angularVelRange = { -90.0f, 90.0f };

    e.meshVertexOffset = 0;
    e.meshVertexCount = 0;

    e.isActive = 1.0f;
    e.emitterID = 9999;                   // 通常の VFX と区別できる値

    e.atlasRows = 1;
    e.atlasCols = 1;
    e.atlasIndex = 0;                     // 0以上 = 固定コマ（アニメしない）
    e.textureIndex = 0;

    e.colorKeyOffset = 0;
    e.colorKeyCount = 0;                  // 0 = startColor / endColor の線形補間

    std::vector<GPUEmitter> emitters{ e };
    std::vector<ColorKey>   keys;         // 空

    m_ParticleSystem.SubmitEmitters(emitters, keys);
    m_LastBurstRequest = (size_t)m_BurstCount;
}

// ============================================================
// 負荷テストのパネル
// ============================================================
void VFXEditorScene::DrawStressUI()
{
    if (!ImGui::CollapsingHeader("Burst Test", ImGuiTreeNodeFlags_DefaultOpen))
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
    // プールを超える要求。護欄が効いているかの確認用。
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

    // ---- 発射パラメータ ----
    ImGui::DragFloat3("Origin", m_BurstOrigin, 0.1f);
    ImGui::DragFloat2("Speed  min/max", m_BurstSpeed, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat2("Life   min/max", m_BurstLife, 0.1f, 0.1f, 30.0f);
    ImGui::DragFloat2("Size   start/end", m_BurstSize, 0.005f, 0.001f, 2.0f);
    ImGui::DragFloat("Gravity", &m_BurstGravity, 0.1f, -30.0f, 30.0f);
    ImGui::DragFloat("Drag", &m_BurstDrag, 0.02f, 0.0f, 5.0f);

    ImGui::Separator();

    // ---- 結果 ----
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Result");
    ImGui::Text("Requested this frame : %zu", m_LastBurstRequest);
    ImGui::Text("Emitters submitted   : %zu / %zu",
        m_LastEmitterCount, m_ParticleSystem.GetMaxEmitters());

    // 実際に何発出たかを CPU は知らない。これは仕様であって欠陥ではない。
    //   知ろうとすると GPU の完了を待つことになる。
    ImGui::TextDisabled("How many actually spawned lives on the GPU only.");
    ImGui::TextDisabled("Judge it by the screen.");

    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Flush CPU Time");
    ImGui::TextDisabled("Flush only queues commands. Requesting 100k should");
    ImGui::TextDisabled("not change this number. If it does, we wait for the GPU.");

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

    // GetAliveCount() は現在 1 を返す暫定実装。
    //   毎フレームの回読（Map READ）を廃止したため、
    //   正確な生存数は GPU 上にしか無い。ここに出すと嘘になる。
    ImGui::TextDisabled("Alive count lives on the GPU only.");
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

    // ---------- 負荷テスト ----------
    DrawStressUI();

    // ---------- 環境 ----------
    if (ImGui::CollapsingHeader("Environment"))
    {
        ImGui::DragFloat3("Light Dir", m_LightDir, 0.02f, -1.0f, 1.0f);
        ImGui::ColorEdit3("Ambient", m_AmbientColor);
    }

    ImGui::End();
}