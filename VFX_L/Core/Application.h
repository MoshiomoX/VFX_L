#pragma once
#include "Core/Window.h"
#include "Graphics/Graphics.h"
#include "Core/Timer/EngineTimer.h"
#include "Graphics/Renderer/Renderer.h"
#include "Core/Game.h"
#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

class Application
{
public:
    bool Initialize();
    void Run();
    void Shutdown();

    static Application& Get() { return *s_Instance; }
    
    
    Window& GetWindow() { return m_Window; }
    Graphics& GetGraphics() { return m_Graphics; }
    EngineTimer& GetTimer() { return m_Timer; }
    Renderer& GetRenderer() { return m_Renderer; }
	Game& GetGame() { return m_Game; }
private:
 
    Window m_Window;
    Graphics m_Graphics;
    EngineTimer m_Timer;
    Renderer m_Renderer;

	Game m_Game;
    bool m_IsRunning = false;

    static Application* s_Instance;
};
