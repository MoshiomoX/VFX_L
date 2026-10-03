// ============================================================
// BattleCamera.cpp
// ============================================================
#include "Camera/BattleCamera.h"
#include "Collider/CollisionSystem.h"
#include "Debug/DebugManager.h"
#include "Manager/InputManager.h"
#include "ResourcePaths.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>

// ============================================================
// 保存する調整値の一覧（保存・読込・既定値に戻す で共用）。
// FollowCamera に値を増やしたらここに 1 行
// ============================================================
#define CAMERA_FLOATS(X) \
    X(fov) X(defaultYaw) X(defaultPitch) \
    X(distance) X(height) X(shoulderOffset) \
    X(stickSensitivity) X(mouseSensitivity) X(pitchMin) X(pitchMax) \
    X(followSmoothTime) X(verticalSmoothTime) X(snapDistance) \
    X(probeRadius) X(minDistance) X(returnSmoothTime) \
    X(shakeMaxYaw) X(shakeMaxPitch) X(shakeMaxRoll) X(shakeFrequency) X(traumaDecay) \
    X(speedStart) X(speedFovPerMps) X(speedFovMax) X(speedDistPerMps) X(speedDistMax) X(speedSmoothTime) \
    X(lookAheadTime) X(lookAheadMax) X(lookAheadSmoothTime) \
    X(zoomMin) X(zoomMax) X(zoomStep)

#define CAMERA_BOOLS(X) \
    X(invertY) X(smoothFollow) X(avoidOcclusion) X(shakeEnabled) X(speedEffects) X(wheelZoom)

// 揺れのきっかけ（BattleCamera 側）
#define CAMERA_TRIGGER_FLOATS(X) \
    X(m_HitTraumaBase, "hitTraumaBase") X(m_HitTraumaPerDamage, "hitTraumaPerDamage") X(m_HitTraumaMax, "hitTraumaMax") \
    X(m_AreaTrauma, "areaTrauma") X(m_AreaTraumaMax, "areaTraumaMax")

#define CAMERA_TRIGGER_BOOLS(X) \
    X(m_ShakeOnHit, "shakeOnHit") X(m_ShakeOnArea, "shakeOnArea")

void BattleCamera::Init(float aspect, CollisionSystem* terrain)
{
    LoadSettings();                 // 保存済みの調整があれば使う
    m_Camera.SetAspect(aspect);     // fov は調整値から
    m_Camera.ResetView();
    m_Camera.SnapToTarget();
    m_CursorFree = false;   // やり直し・F5 の後はカーソルを隠した状態から

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
    m_Camera.SetAspect(aspect);
}

// ============================================================
// 保存 / 読込 / 既定値
// 読込は「キーが無ければ今の値のまま」（項目を増やしても古い json が読める）
// ============================================================
bool BattleCamera::SaveSettings(const char* path) const
{
    const char* file = path ? path : Res::Cfg::Camera;
    nlohmann::json j;
#define X(f) j[#f] = m_Camera.f;
    CAMERA_FLOATS(X) CAMERA_BOOLS(X)
#undef X
#define X(m, key) j[key] = m;
    CAMERA_TRIGGER_FLOATS(X) CAMERA_TRIGGER_BOOLS(X)
#undef X

    std::ofstream ofs(file);
    if (!ofs.is_open())
    {
        std::cout << "[Error] Camera: failed to save " << file << std::endl;
        return false;
    }
    ofs << j.dump(4);
    std::cout << "[OK] Camera settings saved: " << file << std::endl;
    return true;
}

bool BattleCamera::LoadSettings(const char* path)
{
    const char* file = path ? path : Res::Cfg::Camera;
    std::ifstream ifs(file);
    if (!ifs.is_open())
    {
        std::cout << "[Info] Camera: no settings file, using defaults" << std::endl;
        return false;
    }
    nlohmann::json j;
    try { ifs >> j; }
    catch (const nlohmann::json::exception& e)
    {
        std::cout << "[Error] Camera: json parse error: " << e.what() << std::endl;
        return false;
    }
#define X(f) if (j.contains(#f) && j[#f].is_number()) m_Camera.f = j[#f].get<float>();
    CAMERA_FLOATS(X)
#undef X
#define X(f) if (j.contains(#f) && j[#f].is_boolean()) m_Camera.f = j[#f].get<bool>();
    CAMERA_BOOLS(X)
#undef X
#define X(m, key) if (j.contains(key) && j[key].is_number()) m = j[key].get<float>();
    CAMERA_TRIGGER_FLOATS(X)
#undef X
#define X(m, key) if (j.contains(key) && j[key].is_boolean()) m = j[key].get<bool>();
    CAMERA_TRIGGER_BOOLS(X)
#undef X
    std::cout << "[OK] Camera settings loaded: " << file << std::endl;
    return true;
}

void BattleCamera::ResetSettings()
{
    const FollowCamera def;
#define X(f) m_Camera.f = def.f;
    CAMERA_FLOATS(X) CAMERA_BOOLS(X)
#undef X
    const BattleCamera defTrig;
#define X(m, key) m = defTrig.m;
    CAMERA_TRIGGER_FLOATS(X) CAMERA_TRIGGER_BOOLS(X)
#undef X
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
// 爆発：鏡頭を揺らす印（AreaProfile::cameraShake）の付いた範囲が出た数（GPU が数える）。
//   命中で出る範囲は GPU が作るので CPU には位置が来ない → 距離で弱めることはできない
// どちらも trauma を「足す」のではなく「少なくともそこまで上げる」（2026-10-03、用户：特効を出し切ると画面が揺れ続ける）。
//   以前は GPU の範囲の数が増える度に足していて、命中の火花・死んだ時の土煙（どちらも範囲）が毎秒十数個出ると
//   減衰（毎秒 1.4）を上回って trauma が 1 に張り付いていた
// ============================================================
void BattleCamera::OnPlayerHit(float hpLost)
{
    if (m_ShakeOnHit && hpLost > 0.0f)
        m_Camera.RaiseTrauma((std::min)(m_HitTraumaMax, m_HitTraumaBase + hpLost * m_HitTraumaPerDamage));
}

void BattleCamera::OnShakeAreas(uint32_t count)
{
    if (m_ShakeOnArea && count > 0)
        m_Camera.RaiseTrauma((std::min)(m_AreaTraumaMax, (float)count * m_AreaTrauma));
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

    // ---- 保存 / 読込 ----
    if (ImGui::Button("Save")) SaveSettings();
    ImGui::SameLine();
    if (ImGui::Button("Load")) LoadSettings();
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) ResetSettings();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Res::Cfg::Camera);

    ImGui::Text("Yaw/Pitch : %.1f / %.1f   speed %.1f m/s   fov %.1f   dist %.2f (+%.2f speed)",
        m_Camera.GetYaw(), m_Camera.GetPitch(), m_Camera.GetSpeed(), m_Camera.GetEffectiveFov(),
        m_Camera.GetCurrentDistance(), m_Camera.GetSpeedExtraDistance());
    ImGui::Text("Mouse look : %s (Alt toggles cursor)",
        InputManager::Get().IsMouseCaptured() ? "ON" : "off");

    ImGui::SeparatorText("Position / angle");
    ImGui::DragFloat("Distance", &m_Camera.distance, 0.1f, 1.0f, 30.0f);
    ImGui::DragFloat("Height", &m_Camera.height, 0.05f, 0.0f, 5.0f);
    ImGui::DragFloat("Shoulder (+R / -L)", &m_Camera.shoulderOffset, 0.02f, -2.0f, 2.0f);
    ImGui::DragFloat("FOV (deg)", &m_Camera.fov, 0.25f, 20.0f, 110.0f);
    ImGui::DragFloat("Default yaw", &m_Camera.defaultYaw, 0.5f, -180.0f, 180.0f);
    ImGui::DragFloat("Default pitch", &m_Camera.defaultPitch, 0.25f, -30.0f, 80.0f);
    if (ImGui::Button("Reset view (default yaw / pitch)")) m_Camera.ResetView();
    ImGui::SameLine();
    if (ImGui::Button("Use current as default"))
    {
        m_Camera.defaultYaw = m_Camera.GetYaw();
        m_Camera.defaultPitch = m_Camera.GetPitch();
    }
    ImGui::DragFloatRange2("Pitch limits", &m_Camera.pitchMin, &m_Camera.pitchMax, 0.5f, -89.0f, 89.0f);

    ImGui::SeparatorText("Input");
    ImGui::DragFloat("Stick Sens", &m_Camera.stickSensitivity, 1.0f, 10.0f, 500.0f);
    ImGui::DragFloat("Mouse Sens", &m_Camera.mouseSensitivity, 0.01f, 0.01f, 1.0f);
    ImGui::Checkbox("Invert Y", &m_Camera.invertY);
    ImGui::Checkbox("Wheel zoom", &m_Camera.wheelZoom);
    ImGui::DragFloatRange2("Zoom range (m)", &m_Camera.zoomMin, &m_Camera.zoomMax, 0.1f, 1.0f, 40.0f);
    ImGui::DragFloat("Zoom step (m)", &m_Camera.zoomStep, 0.05f, 0.05f, 5.0f);

    ImGui::SeparatorText("Speed feel (slide / fast run)");
    ImGui::Checkbox("Enabled##speed", &m_Camera.speedEffects);
    ImGui::DragFloat("Start speed (m/s)", &m_Camera.speedStart, 0.1f, 0.0f, 40.0f);
    ImGui::DragFloat("FOV per m/s", &m_Camera.speedFovPerMps, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("FOV max", &m_Camera.speedFovMax, 0.25f, 0.0f, 60.0f);
    ImGui::DragFloat("Pull back per m/s", &m_Camera.speedDistPerMps, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Pull back max (m)", &m_Camera.speedDistMax, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Speed smoothing (s)", &m_Camera.speedSmoothTime, 0.01f, 0.0f, 2.0f);

    ImGui::SeparatorText("Look-ahead");
    ImGui::DragFloat("Time (s, 0 = off)", &m_Camera.lookAheadTime, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Max (m)", &m_Camera.lookAheadMax, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Smoothing (s)", &m_Camera.lookAheadSmoothTime, 0.01f, 0.0f, 2.0f);

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
    ImGui::Checkbox("On explosion (area json: cameraShake)", &m_ShakeOnArea);
    ImGui::DragFloat("Per explosion", &m_AreaTrauma, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Explosion max", &m_AreaTraumaMax, 0.01f, 0.0f, 1.0f);
    ImGui::TextDisabled("hit / explosion raise trauma to at least the value (no stacking)");
    if (ImGui::Button("Test 0.3")) m_Camera.AddTrauma(0.3f);
    ImGui::SameLine();
    if (ImGui::Button("Test 0.6")) m_Camera.AddTrauma(0.6f);
    ImGui::SameLine();
    if (ImGui::Button("Test 1.0")) m_Camera.AddTrauma(1.0f);
}
