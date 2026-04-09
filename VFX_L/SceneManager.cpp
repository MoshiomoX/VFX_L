#include "SceneManager.h"
#include <iostream>

void SceneManager::ChangeScene(SceneType type)
{
    if (m_CurrentScene)
    {
        m_CurrentScene->Shutdown();
    }

    auto it = m_SceneFactory.find(type);
    if (it != m_SceneFactory.end())
    {
        m_CurrentScene = it->second();
        m_CurrentScene->Init();
        m_CurrentSceneType = type;
        std::cout << "[OK] Scene changed" << std::endl;
    }
    else
    {
        std::cout << "[Error] Scene not registered" << std::endl;
    }
}

void SceneManager::Update(float dt)
{
    if (m_CurrentScene)
    {
        m_CurrentScene->Update(dt);
    }
}

void SceneManager::Render(Renderer& renderer)
{
    if (m_CurrentScene)
    {
        m_CurrentScene->Render(renderer);
    }
}