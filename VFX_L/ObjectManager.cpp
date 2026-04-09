#include "ObjectManager.h"

GameObject* ObjectManager::CreateObject()
{
    auto obj = std::make_unique<GameObject>();
    GameObject* ptr = obj.get();
    m_Objects.push_back(std::move(obj));
    return ptr;
}

void ObjectManager::DestroyObject(GameObject* obj)
{
    m_PendingDestroy.push_back(obj);
}

void ObjectManager::Clear()
{
    m_Objects.clear();
    m_PendingDestroy.clear();
}

void ObjectManager::Update(float dt)
{
    // XV
    for (auto& obj : m_Objects)
    {
        obj->Update(dt);
    }

    // íœˆ—
    ProcessDestroy();
}

void ObjectManager::ProcessDestroy()
{
    for (auto* obj : m_PendingDestroy)
    {
        auto it = std::find_if(
            m_Objects.begin(),
            m_Objects.end(),
            [obj](const std::unique_ptr<GameObject>& p)
            {
                return p.get() == obj;
            });

        if (it != m_Objects.end())
        {
            m_Objects.erase(it);
        }
    }
    m_PendingDestroy.clear();
}