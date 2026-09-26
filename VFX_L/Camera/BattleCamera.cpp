// ============================================================
// BattleCamera.cpp
// ============================================================
#include "Camera/BattleCamera.h"
#include "Collider/CollisionSystem.h"
#include "Debug/DebugManager.h"
#include "Manager/InputManager.h"
#include "imgui.h"
#include <algorithm>

void BattleCamera::Init(float aspect, CollisionSystem* terrain)
{
    m_Camera.Init(45.0f, aspect, 0.1f, 10000.0f);
    m_Camera.SnapToTarget();
    m_CursorFree = false;   // やり直し・F5 の後はカーソルを隠した状態から
    m_PrevAliveAreas = 0;

    // 遮蔽回避の射線は地形（床・壁・障害物・宝箱）にだけ当てる。
    // 雑魚は GPU なので当たらず、精英（Layer_Enemy）も除く（敵の陰に入るたびに寄ると酔う）
    if (terrain)
    {
        m_Camera.SetOcclusionProbe(
            [terrain](const Vector3& from, const Vector3& dir, float maxDist, float& outDist)
            {
                CollisionMath::Ray ray{ from, dir, maxDist };
                const auto hit = terrain->Raycast(ray, Layer_Terrain);
                if (!hit.hit) return false;
                outDist = hit.t;
                return true;
            });
    }
}

void BattleCamera::Resize(float aspect)
{
    m_Camera.Init(45.0f, aspect, 0.1f, 10000.0f);
}

// ============================================================
// マウスの捕獲
// 普段はカーソルを隠して中央に閉じ込め、マウスの移動だけで視点が回る（ボタン不要）。
// Alt 単押しでカーソルを出す / しまう（押しっぱなしではなく切り替え。ゲームは止めない）。
// cursorNeeded（UI が開いている / 死んだ後）とデバッグカメラ中は Alt に関係なく出し、
// その間は Alt を受けない（閉じた時に、知らないうちに切り替わっていた、を防ぐ）。
// 要求は毎フレーム出す（出さなくなれば InputManager が放す。場面を抜けた時も同じ）
// ============================================================
void BattleCamera::UpdateMouseCapture(bool cursorNeeded)
{
    if (cursorNeeded || DebugManager::Get().IsUsingDebugCamera()) return;

    auto& input = InputManager::Get();
    if (input.GetAltTap()) m_CursorFree = !m_CursorFree;
    if (!m_CursorFree) input.RequestMouseCapture();
}

// ============================================================
// 画面の揺れ
// 被弾：HP の減りを見る。雑魚（GPU）・精英の接触・デバッグの被弾、経路を問わず拾える。
//   無敵中は TryApplyHit が HP を減らさないので、揺れも自然に止まる
// 範囲攻撃（爆発など）：GPU 上で生まれた範囲の数が増えたら揺らす。
//   命中で出る範囲は GPU が作るので CPU には位置が来ない → 距離で弱めることはできない
// ============================================================
void BattleCamera::OnPlayerHit(float hpLost)
{
    if (m_ShakeOnHit && hpLost > 0.0f)
        m_Camera.AddTrauma((std::min)(m_HitTraumaMax, m_HitTraumaBase + hpLost * m_HitTraumaPerDamage));
}

void BattleCamera::OnAliveAreas(uint32_t aliveAreas)
{
    if (m_ShakeOnArea && aliveAreas > m_PrevAliveAreas)
        m_Camera.AddTrauma((std::min)(m_AreaTraumaMax, (float)(aliveAreas - m_PrevAliveAreas) * m_AreaTrauma));
    m_PrevAliveAreas = aliveAreas;
}

void BattleCamera::Update(float dt, const Vector3* target)
{
    if (target) m_Camera.SetFollowTarget(*target);
    m_Camera.Update(dt);
}

// ============================================================
// ImGui: Camera 面板
// ============================================================
void BattleCamera::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Camera"))
        return;

    ImGui::DragFloat("Distance", &m_Camera.distance, 0.1f, 1.0f, 30.0f);
    ImGui::DragFloat("Height", &m_Camera.height, 0.05f, 0.0f, 5.0f);
    ImGui::DragFloat("Shoulder (+R / -L)", &m_Camera.shoulderOffset, 0.02f, -2.0f, 2.0f);
    ImGui::DragFloat("Stick Sens", &m_Camera.stickSensitivity, 1.0f, 10.0f, 500.0f);
    ImGui::DragFloat("Mouse Sens", &m_Camera.mouseSensitivity, 0.01f, 0.01f, 1.0f);
    ImGui::Checkbox("Invert Y", &m_Camera.invertY);
    ImGui::Text("Mouse look : %s (Alt toggles cursor)",
        InputManager::Get().IsMouseCaptured() ? "ON" : "off");
    ImGui::Text("Yaw/Pitch : %.1f / %.1f", m_Camera.GetYaw(), m_Camera.GetPitch());

    ImGui::SeparatorText("Smooth follow");
    ImGui::Checkbox("Enabled##follow", &m_Camera.smoothFollow);
    ImGui::DragFloat("Horizontal (s)", &m_Camera.followSmoothTime, 0.005f, 0.0f, 1.0f, "%.3f");
    ImGui::DragFloat("Vertical (s)", &m_Camera.verticalSmoothTime, 0.005f, 0.0f, 1.0f, "%.3f");
    ImGui::DragFloat("Snap if farther (m)", &m_Camera.snapDistance, 0.1f, 1.0f, 50.0f);

    ImGui::SeparatorText("Occlusion");
    ImGui::Checkbox("Enabled##occl", &m_Camera.avoidOcclusion);
    ImGui::DragFloat("Probe radius", &m_Camera.probeRadius, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Min distance", &m_Camera.minDistance, 0.05f, 0.1f, 10.0f);
    ImGui::DragFloat("Return (s)", &m_Camera.returnSmoothTime, 0.01f, 0.0f, 2.0f);
    ImGui::Text("Current distance : %.2f %s", m_Camera.GetCurrentDistance(),
        m_Camera.IsOccluded() ? "(occluded)" : "");

    ImGui::SeparatorText("Shake");
    ImGui::Checkbox("Enabled##shake", &m_Camera.shakeEnabled);
    ImGui::DragFloat("Max yaw", &m_Camera.shakeMaxYaw, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Max pitch", &m_Camera.shakeMaxPitch, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Max roll", &m_Camera.shakeMaxRoll, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Frequency", &m_Camera.shakeFrequency, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat("Decay /s", &m_Camera.traumaDecay, 0.05f, 0.1f, 10.0f);
    ImGui::ProgressBar(m_Camera.GetTrauma(), ImVec2(-1, 0), "trauma");

    ImGui::Checkbox("On hit", &m_ShakeOnHit);
    ImGui::DragFloat("Hit base", &m_HitTraumaBase, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Hit per damage", &m_HitTraumaPerDamage, 0.001f, 0.0f, 0.2f, "%.3f");
    ImGui::DragFloat("Hit max", &m_HitTraumaMax, 0.01f, 0.0f, 1.0f);
    ImGui::Checkbox("On area spawn (explosions)", &m_ShakeOnArea);
    ImGui::DragFloat("Per area", &m_AreaTrauma, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Area max", &m_AreaTraumaMax, 0.01f, 0.0f, 1.0f);
    if (ImGui::Button("Test 0.3")) m_Camera.AddTrauma(0.3f);
    ImGui::SameLine();
    if (ImGui::Button("Test 0.6")) m_Camera.AddTrauma(0.6f);
    ImGui::SameLine();
    if (ImGui::Button("Test 1.0")) m_Camera.AddTrauma(1.0f);
}
