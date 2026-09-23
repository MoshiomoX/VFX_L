#include "Scene/SceneManager.h"
#include <iostream>

void SceneManager::ChangeScene(SceneType type)
{
    auto it = m_SceneFactory.find(type);
    if (it == m_SceneFactory.end())
    {
        std::cout << "[Error] Scene not registered: " << SceneTypeName(type) << std::endl;
        return;
    }

    // 旧シーンを完全に壊してから新シーンを作る（GPU 資源の二重確保を避ける）
    if (m_CurrentScene)
    {
        m_CurrentScene->Shutdown();
        m_CurrentScene.reset();
        m_CurrentSceneType = SceneType::NONE;
    }

    m_CurrentScene = it->second();
    m_CurrentSceneType = type;
    m_CurrentScene->Init();
    std::cout << "[OK] Scene changed: " << SceneTypeName(type) << std::endl;
}

void SceneManager::RequestChangeScene(SceneType type)
{
    if (!IsRegistered(type))
    {
        std::cout << "[Error] Scene not registered: " << SceneTypeName(type) << std::endl;
        return;
    }
    m_PendingScene = type;
}

void SceneManager::Update(float dt)
{
    // 依頼された切替はここで実行する。旧シーンの Update の外なので安全
    if (m_PendingScene != SceneType::NONE)
    {
        SceneType next = m_PendingScene;
        m_PendingScene = SceneType::NONE;
        ChangeScene(next);
    }

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
