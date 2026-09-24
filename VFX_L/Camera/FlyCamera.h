// ============================================================
// FlyCamera.h
// Unity のシーンビューと同じ操作の自由カメラ（編集用）。
//
//   右ボタンを押している間 … マウスで見回し + WASD で移動、E / Q で上下、
//                            Shift で速く
//   ホイール              … 向いている方向へ前後
//   Focus()               … 指定した球が画面に収まる位置へ寄る（F キー用）
//
//   右ボタンを ImGui の窓の上で押した時は反応しない（窓のスクロール等と取り合わない）。
//   左ボタンは使わない（物の選択・ギズモに残す）
// ============================================================
#pragma once
#include "Camera/CameraBase.h"

class FlyCamera : public CameraBase
{
public:
    void Update(float dt) override;

    // 位置と向き（度。yaw 0 = +Z 向き、pitch 正 = 見下ろす）を直接決める
    void Place(const Vector3& pos, float yawDeg, float pitchDeg);

    // center / radius の球が収まる距離まで、今の向きのまま寄る
    void Focus(const Vector3& center, float radius);

    // 右ボタンで見回している最中（この間はキーを他の操作に使わない）
    bool IsLooking() const { return m_Looking; }

    float moveSpeed = 12.0f;       // m/秒
    float fastMultiplier = 3.0f;   // Shift
    float lookSensitivity = 0.15f; // 度/px
    float wheelStep = 2.0f;        // ホイール 1 目盛りで進む m

private:
    void Apply();   // yaw/pitch/position → CameraBase の position/target

    float m_Yaw = 0.0f;
    float m_Pitch = 30.0f;
    bool  m_Looking = false;
};
