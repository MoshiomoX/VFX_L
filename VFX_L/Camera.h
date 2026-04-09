#pragma once
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

class Camera
{
public:
    void Init(float fov, float aspect, float nearZ, float farZ);

    // 設定
    void SetPosition(const Vector3& pos);
    void SetTarget(const Vector3& target);
    void SetUp(const Vector3& up);
    void LookAt(const Vector3& pos, const Vector3& target, const Vector3& up);

    // 取得
    Matrix GetViewMatrix();
    Matrix GetProjectionMatrix() const { return m_Projection; }
    Vector3 GetPosition() const { return m_Position; }
    Vector3 GetForward() const;
    Vector3 GetRight() const;
    Vector3 GetUp() const { return m_Up; }

private:
    void UpdateViewMatrix();

private:
    Vector3 m_Position = { 0.0f, 0.0f, 0.0f };
    Vector3 m_Target = { 0.0f, 0.0f, 0.0f };
    Vector3 m_Up = { 0.0f, 1.0f, 0.0f };

    Matrix m_View;
    Matrix m_Projection;

    bool m_Dirty = true;
};