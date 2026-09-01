// ============================================================
// FollowCamera.h
// TPS 追従カメラ：対象の後上方から追いかけ、右スティック／マウスで旋回する。
// 距離・高さ・感度は外部から調整可能。
// ============================================================
#pragma once
#include "Camera/CameraBase.h"

class FollowCamera : public CameraBase
{
public:
    void Update(float dt) override;

    // 追従対象のワールド座標（毎フレーム、物理更新後に渡す）
    void SetFollowTarget(const Vector3& pos) { m_FollowTarget = pos; }

    float GetYaw()   const { return m_Yaw; }
    float GetPitch() const { return m_Pitch; }

    // ---- 調整パラメータ（ImGui から触る）----
    float distance = 6.0f;    // 対象からの距離（中距離）
    float height = 1.5f;    // 注視点の高さオフセット（足元でなく胸あたりを見る）
    float stickSensitivity = 150.0f;  // 度/秒
    float mouseSensitivity = 0.15f;   // 度/ピクセル
    float pitchMin = -30.0f;  // 見上げ限界
    float pitchMax = 70.0f;  // 見下ろし限界
    bool  invertY = false;

private:
    Vector3 m_FollowTarget = { 0, 0, 0 };
    float   m_Yaw = 0.0f;    // 水平角（度）
    float   m_Pitch = 20.0f;   // 仰角（度、正=見下ろす）
};