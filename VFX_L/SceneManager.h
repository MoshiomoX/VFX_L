#pragma once
#include <memory>
#include <unordered_map>
#include <functional>
#include "SceneBase.h"
#include "SceneType.h"

class Renderer;

class SceneManager
{
public:
    template<typename T>
    void RegisterScene(SceneType type)
    {
        m_SceneFactory[type] = []() { return std::make_unique<T>(); };
    }

    void ChangeScene(SceneType type);

    void Update(float dt);
    void Render(Renderer& renderer);

    SceneBase* GetCurrentScene() const { return m_CurrentScene.get(); }
    SceneType GetCurrentSceneType() const { return m_CurrentSceneType; }

private:
    std::unique_ptr<SceneBase> m_CurrentScene;
    SceneType m_CurrentSceneType = SceneType::NONE;
    std::unordered_map<SceneType, std::function<std::unique_ptr<SceneBase>()>> m_SceneFactory;
};