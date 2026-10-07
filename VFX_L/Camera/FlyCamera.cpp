// ============================================================
// FlyCamera.cpp
// ============================================================
#include "Camera/FlyCamera.h"
#include "Manager/InputManager.h"
#include "imgui.h"

namespace
{
    // yaw / pitch（度）→ 前方。FollowCamera と同じ規約（右手系で yaw 正 = 右を向く）
    Vector3 ForwardOf(float yawDeg, float pitchDeg)
    {
        const float y = DirectX::XMConvertToRadians(yawDeg);
        const float p = DirectX::XMConvertToRadians(pitchDeg);
        Vector3 f(-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p));
        f.Normalize();
        return f;
    }
}

void FlyCamera::Place(const Vector3& pos, float yawDeg, float pitchDeg)
{
    m_Position = pos;
    m_Yaw = yawDeg;
    m_Pitch = std::clamp(pitchDeg, -89.0f, 89.0f);
    Apply();
}

void FlyCamera::Focus(const Vector3& center, float radius)
{
    // 縦 45 度の画角に半径 r の球が収まる距離 ≒ r / sin(22.5°)。少し余白を足す
    const float dist = (std::max)(radius, 0.5f) / std::sin(DirectX::XMConvertToRadians(22.5f)) * 1.1f;
    m_Position = center - ForwardOf(m_Yaw, m_Pitch) * dist;
    Apply();
}

void FlyCamera::Apply()
{
    m_Target = m_Position + ForwardOf(m_Yaw, m_Pitch);
    m_Up = Vector3::UnitY;
    m_Dirty = true;
}

void FlyCamera::Update(float dt)
{
    auto& input = InputManager::Get();
    const ImGuiIO& io = ImGui::GetIO();

    // ---- 見回しの開始と終了 ----
    // 押し始めが ImGui の窓の上なら始めない。始めたら離すまで続ける（窓の上を通っても切れない）
    if (!m_Looking && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse)
        m_Looking = true;
    if (m_Looking && !input.GetMousePress(1))
        m_Looking = false;

    if (m_Looking)
    {
        const auto md = input.GetMouseDelta();
        m_Yaw += md.x * lookSensitivity;
        m_Pitch = std::clamp(m_Pitch + md.y * lookSensitivity, -89.0f, 89.0f);
        if (m_Yaw > 180.0f) m_Yaw -= 360.0f;
        if (m_Yaw < -180.0f) m_Yaw += 360.0f;

        const Vector3 fwd = ForwardOf(m_Yaw, m_Pitch);
        Vector3 right = fwd.Cross(Vector3::UnitY);
        right.Normalize();

        Vector3 move = Vector3::Zero;
        if (input.GetKeyPress('W')) move += fwd;
        if (input.GetKeyPress('S')) move -= fwd;
        if (input.GetKeyPress('D')) move += right;
        if (input.GetKeyPress('A')) move -= right;
        if (input.GetKeyPress('E')) move += Vector3::UnitY;
        if (input.GetKeyPress('Q')) move -= Vector3::UnitY;
        if (move.LengthSquared() > 1e-6f)
        {
            move.Normalize();
            const float speed = moveSpeed * (input.GetKeyPress(VK_SHIFT) ? fastMultiplier : 1.0f);
            m_Position += move * speed * dt;
        }
    }

    // ---- ホイールで前後（ImGui の窓の上では窓のスクロールに譲る）----
    // InputManager のホイール値はフレーム頭で消えるので、ImGui が受けた値を使う
    if (!io.WantCaptureMouse && io.MouseWheel != 0.0f)
        m_Position += ForwardOf(m_Yaw, m_Pitch) * (io.MouseWheel * wheelStep);

    Apply();
}
