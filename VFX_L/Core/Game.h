#pragma once
#include "Scene/SceneManager.h"
//#include "Manager/InputManager.h"
class Renderer;

class Game
{
public:
    Game();
    ~Game();

    bool Initialize(Renderer* renderer);
    void Update(float dt);
    void Render();
    // 終了時（Application::Shutdown の最初）：今のシーンを Shutdown して壊す。以後 Update / Render は何もしない
    void Shutdown();

    SceneManager& GetSceneManager() { return m_SceneManager; }

private:
    SceneManager m_SceneManager;
    Renderer* m_Renderer = nullptr;  // ???,???

    bool m_IsRunning = true;	
};