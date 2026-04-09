#include "Camera.h"

void Camera::Init(float fov, float aspect, float nearZ, float farZ)
{
    m_Projection = Matrix::CreatePerspectiveFieldOfView
    (
        DirectX::XMConvertToRadians(fov),
        aspect,
        nearZ,
        farZ
    );

    m_Dirty = true;
}

void Camera::SetPosition(const Vector3& pos)
{
    m_Position = pos;
    m_Dirty = true;
}

void Camera::SetTarget(const Vector3& target)
{
    m_Target = target;
    m_Dirty = true;
}

void Camera::SetUp(const Vector3& up)
{
    m_Up = up;
    m_Dirty = true;
}

void Camera::LookAt(const Vector3& pos, const Vector3& target, const Vector3& up)
{
    m_Position = pos;
    m_Target = target;
    m_Up = up;
    m_Dirty = true;
}

Matrix Camera::GetViewMatrix()
{
    if (m_Dirty)
    {
        UpdateViewMatrix();
        m_Dirty = false;
    }
    return m_View;
}

void Camera::UpdateViewMatrix()
{
    m_View = Matrix::CreateLookAt(m_Position, m_Target, m_Up);
}

Vector3 Camera::GetForward() const
{
    Vector3 forward = m_Target - m_Position;
    forward.Normalize();
    return forward;
}

Vector3 Camera::GetRight() const
{
    Vector3 forward = GetForward();
    Vector3 right = m_Up.Cross(forward);
    right.Normalize();
    return right;
}
