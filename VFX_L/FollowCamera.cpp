// ============================================================
// FollowCamera.cpp
// ============================================================
#include "FollowCamera.h"
#include "InputManager.h"
#include "DebugManager.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>

void FollowCamera::Update(float dt)
{
    // デバッグカメラ操作中・ImGui 操作中は視点入力を受け付けない
    bool acceptInput = !DebugManager::Get().IsUsingDebugCamera()
        && !ImGui::GetIO().WantCaptureMouse;

    if (acceptInput)
    {
        auto& input = InputManager::Get();

        float yawDelta = 0.0f;
        float pitchDelta = 0.0f;   // 正 = 見下ろす方向

        // --- 右スティック ---
        Vector2 rs = input.GetPadRightStick();
        yawDelta += rs.x * stickSensitivity * dt;
        pitchDelta -= rs.y * stickSensitivity * dt;   // 上に倒す = 見上げる

        // --- マウス右ドラッグ ---
        if (input.GetMousePress(1))
        {
            auto md = input.GetMouseDelta();
            yawDelta += md.x * mouseSensitivity;
            pitchDelta += md.y * mouseSensitivity;    // 下に動かす = 見下ろす
        }

        if (invertY) pitchDelta = -pitchDelta;

        m_Yaw += yawDelta;
        m_Pitch += pitchDelta;
        m_Pitch = std::clamp(m_Pitch, pitchMin, pitchMax);

        // yaw を -180〜180 に丸める（数値の肥大を防ぐ）
        if (m_Yaw > 180.0f) m_Yaw -= 360.0f;
        if (m_Yaw < -180.0f) m_Yaw += 360.0f;
    }

    // --- yaw / pitch からカメラの前方向を作る ---
    float yawRad = DirectX::XMConvertToRadians(m_Yaw);
    float pitchRad = DirectX::XMConvertToRadians(m_Pitch);

    Vector3 forward;
    forward.x = -std::sin(yawRad) * std::cos(pitchRad);
    forward.y = -std::sin(pitchRad);              // pitch 正 = 下向き
    forward.z = std::cos(yawRad) * std::cos(pitchRad);
    forward.Normalize();

    // 注視点は対象の少し上、カメラはそこから forward の逆方向へ distance 分下がる
    Vector3 lookAt = m_FollowTarget + Vector3(0.0f, height, 0.0f);
    Vector3 pos = lookAt - forward * distance;

    LookAt(pos, lookAt, Vector3(0.0f, 1.0f, 0.0f));
}