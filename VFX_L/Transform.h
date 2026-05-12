#pragma once
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

class Transform
{
public:
    // ê›íË
    void SetPosition(const Vector3& pos);
    void SetRotation(const Vector3& rot);
    void SetScale(const Vector3& scale);

    // éÊìæ
    Vector3 GetPosition() const { return m_Position; }
    Vector3 GetRotation() const { return m_Rotation; }
    Vector3 GetScale() const { return m_Scale; }
    Matrix GetWorldMatrix();

    // ï˚å¸
    Vector3 GetForward() const;
    Vector3 GetRight() const;   
    Vector3 GetUp() const;

private:
    
    void UpdateWorldMatrix();

private:
    Vector3 m_Position = { 0.0f, 0.0f, 0.0f };
    Vector3 m_Rotation = { 0.0f, 0.0f, 0.0f };
    Vector3 m_Scale = { 1.0f, 1.0f, 1.0f };

    Matrix m_WorldMatrix;
    bool m_Dirty = true;
};