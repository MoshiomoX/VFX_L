#pragma once

class GameObject;

class Component
{
public:
    virtual ~Component() = default;

    virtual void Init() {}
    virtual void Update(float dt) {}

    void SetOwner(GameObject* owner) { m_Owner = owner; }
    GameObject* GetOwner() const { return m_Owner; }

protected:
    GameObject* m_Owner = nullptr;
};