#include "Transform.h"

void Transform::SetPosition(const Vector3& pos)
{
    m_Position = pos;
    m_Dirty = true;
}

void Transform::SetRotation(const Vector3& rot)
{
    m_Rotation = rot;
    m_Dirty = true;
}

void Transform::SetScale(const Vector3& scale)
{
    m_Scale = scale;
    m_Dirty = true;
}

Matrix Transform::GetWorldMatrix()
{
    if (m_Dirty)
    {
        UpdateWorldMatrix();
        m_Dirty = false;
    }
    return m_WorldMatrix;
}

void Transform::UpdateWorldMatrix()
{
    Matrix scale = Matrix::CreateScale(m_Scale);
    Matrix rotation = Matrix::CreateFromYawPitchRoll(
        DirectX::XMConvertToRadians(m_Rotation.y),
        DirectX::XMConvertToRadians(m_Rotation.x),
        DirectX::XMConvertToRadians(m_Rotation.z)
    );
    Matrix translation = Matrix::CreateTranslation(m_Position);

    m_WorldMatrix = scale * rotation * translation;
}

Vector3 Transform::GetForward() const
{
    Matrix rot = Matrix::CreateFromYawPitchRoll(
        DirectX::XMConvertToRadians(m_Rotation.y),
        DirectX::XMConvertToRadians(m_Rotation.x),
        DirectX::XMConvertToRadians(m_Rotation.z)
    );
    return Vector3::TransformNormal(Vector3::Forward, rot);
}

Vector3 Transform::GetRight() const
{
    Matrix rot = Matrix::CreateFromYawPitchRoll(
        DirectX::XMConvertToRadians(m_Rotation.y),
        DirectX::XMConvertToRadians(m_Rotation.x),
        DirectX::XMConvertToRadians(m_Rotation.z)
    );
    return Vector3::TransformNormal(Vector3::Right, rot);
}

Vector3 Transform::GetUp() const
{
    Matrix rot = Matrix::CreateFromYawPitchRoll(
        DirectX::XMConvertToRadians(m_Rotation.y),
        DirectX::XMConvertToRadians(m_Rotation.x),
        DirectX::XMConvertToRadians(m_Rotation.z)
    );
    return Vector3::TransformNormal(Vector3::Up, rot);
}