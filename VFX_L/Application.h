#pragma once
#include "Window.h"
#include "Graphics.h"
#include "EngineTimer.h"
#include "Renderer.h"
#include "Game.h"
#define WINDOW_WIDTH 1600
#define WINDOW_HEIGHT 900

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