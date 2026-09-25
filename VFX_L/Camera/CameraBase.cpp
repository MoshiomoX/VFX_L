#include "Camera/CameraBase.h"

void CameraBase::Init(float fov, float aspect, float nearZ, float farZ)
{
    m_Projection = Matrix::CreatePerspectiveFieldOfView(
        DirectX::XMConvertToRadians(fov), aspect, nearZ, farZ);
    m_Dirty = true;
}

void CameraBase::SetPosition(const Vector3& pos) { m_Position = pos; m_Dirty = true; }
void CameraBase::SetTarget(const Vector3& target) { m_Target = target; m_Dirty = true; }
void CameraBase::SetUp(const Vector3& up) { m_Up = up; m_Dirty = true; }

void CameraBase::LookAt(const Vector3& pos, const Vector3& target, const Vector3& up)
{
    m_Position = pos;
    m_Target = target;
    m_Up = up;
    m_Dirty = true;
}

Matrix CameraBase::GetViewMatrix()
{
    if (m_Dirty) { UpdateViewMatrix(); m_Dirty = false; }
    return m_View;
}

void CameraBase::SetViewShake(float yawDeg, float pitchDeg, float rollDeg)
{
    if (yawDeg == m_ShakeYaw && pitchDeg == m_ShakePitch && rollDeg == m_ShakeRoll) return;
    m_ShakeYaw = yawDeg;
    m_ShakePitch = pitchDeg;
    m_ShakeRoll = rollDeg;
    m_Dirty = true;
}

void CameraBase::UpdateViewMatrix()
{
    m_View = Matrix::CreateLookAt(m_Position, m_Target, m_Up);

    // 揺れはビュー空間で回す（カメラの位置を中心にした首振り）。
    // 位置は動かさないので、GetPosition を使う光・粒子の計算はずれない
    if (m_ShakeYaw != 0.0f || m_ShakePitch != 0.0f || m_ShakeRoll != 0.0f)
    {
        m_View *= Matrix::CreateFromYawPitchRoll(
            DirectX::XMConvertToRadians(m_ShakeYaw),
            DirectX::XMConvertToRadians(m_ShakePitch),
            DirectX::XMConvertToRadians(m_ShakeRoll));
    }
}

Vector3 CameraBase::GetForward() const
{
    Vector3 forward = m_Target - m_Position;
    forward.Normalize();
    return forward;
}

Vector3 CameraBase::GetRight() const
{
    Vector3 forward = GetForward();
    Vector3 right = forward.Cross(m_Up);
    right.Normalize();
    return right;
}