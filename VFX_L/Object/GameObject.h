#pragma once
#include <vector>
#include "Component/Component.h"
#include "Graphics/Transform.h"

class GameObject
{
public:
    ~GameObject()
    {
        for (auto comp : m_Components)
        {
            delete comp;
        }
    }

    // Transform
    Transform& GetTransform() { return m_Transform; }

    // Component??
    template<typename T>
    T* AddComponent()
    {
        T* comp = new T();
        comp->SetOwner(this);
        comp->Init();
        m_Components.push_back(comp);
        return comp;
    }

    // Component??
    template<typename T>
    T* GetComponent()
    {
        for (auto comp : m_Components)
        {
            T* result = dynamic_cast<T*>(comp);
            if (result)
                return result;
        }
        return nullptr;
    }

    void Update(float dt)
    {
        for (auto comp : m_Components)
        {
            comp->Update(dt);
        }
    }

private:
    Transform m_Transform;                  // ??
    std::vector<Component*> m_Components;   // ??
};