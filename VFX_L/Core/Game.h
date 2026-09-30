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

    SceneManager& GetSceneManager() { return m_SceneManager; }

private:
    SceneManager m_SceneManager;
    Renderer* m_Renderer = nullptr;  // ???,???

    bool m_IsRunning = true;	
};