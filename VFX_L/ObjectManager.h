#pragma once
#include <vector>
#include <memory>
#include "GameObject.h"

class ObjectManager
{
public:
    GameObject* CreateObject();
    void DestroyObject(GameObject* obj);
    void Clear();

    void Update(float dt);

    const std::vector<std::unique_ptr<GameObject>>& GetObjects() const
    {   
        return m_Objects;
    }

private:
    std::vector<std::unique_ptr<GameObject>> m_Objects;
    std::vector<GameObject*> m_PendingDestroy;

    void ProcessDestroy();
};