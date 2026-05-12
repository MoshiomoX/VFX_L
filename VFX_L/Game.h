#pragma once
#include "SceneManager.h"
#include "GameTimer.h"
//#include "InputManager.h"
class Renderer;

class Game
{
public:
    Game();
    ~Game();

    bool Initialize(Renderer* renderer);
    void Update(float dt);
    void Render();

    SceneManager& GetSceneManager() { return m_SceneManager; }

private:
    SceneManager m_SceneManager;
    GameTimer m_Timer;
    Renderer* m_Renderer = nullptr;  // 弱引用，不拥有

    bool m_IsRunning = true;	
};