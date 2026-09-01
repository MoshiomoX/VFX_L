#pragma once
#include "Scene/SceneManager.h"
#include "Core/Timer/GameTimer.h"
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

    SceneManager& GetSceneManager() { return m_SceneManager; }

private:
    SceneManager m_SceneManager;
    GameTimer m_Timer;
    Renderer* m_Renderer = nullptr;  // ???,???

    bool m_IsRunning = true;	
};