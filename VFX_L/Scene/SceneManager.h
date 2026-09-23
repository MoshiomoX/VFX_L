#pragma once
#include <memory>
#include <unordered_map>
#include <functional>
#include "Scene/SceneBase.h"
#include "Scene/SceneType.h"

class Renderer;

class SceneManager
{
public:
    template<typename T>
    void RegisterScene(SceneType type)
    {
        m_SceneFactory[type] = []() { return std::make_unique<T>(); };
    }

    // 即時切替。起動時（Game::Initialize）専用。
    // シーンの Update / ImGui の中から呼ぶと自分自身を delete する事になるので、
    // 実行中の切替は RequestChangeScene を使う
    void ChangeScene(SceneType type);

    // 遅延切替。次の Update の先頭（旧シーンの Update の外）で ChangeScene を実行する。
    // 同一フレームに複数回呼ばれたら最後の物が勝つ。
    // 現在と同じ type を渡すと作り直し（リロード）になる
    void RequestChangeScene(SceneType type);
    bool HasPendingChange() const { return m_PendingScene != SceneType::NONE; }
    bool IsRegistered(SceneType type) const { return m_SceneFactory.count(type) != 0; }

    void Update(float dt);
    void Render(Renderer& renderer);

    SceneBase* GetCurrentScene() const { return m_CurrentScene.get(); }
    SceneType GetCurrentSceneType() const { return m_CurrentSceneType; }

private:
    std::unique_ptr<SceneBase> m_CurrentScene;
    SceneType m_CurrentSceneType = SceneType::NONE;
    SceneType m_PendingScene = SceneType::NONE;
    std::unordered_map<SceneType, std::function<std::unique_ptr<SceneBase>()>> m_SceneFactory;
};
