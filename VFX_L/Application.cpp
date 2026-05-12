#include "Application.h"
#include "TestScene.h"
#include "DebugManager.h"
#include <iostream>
#include "ResourceManager.h"
#include "InputManager.h"


Application* Application::s_Instance = nullptr;

bool Application::Initialize()
{
    s_Instance = this;

    // Window
    if (!m_Window.Create(WINDOW_WIDTH, WINDOW_HEIGHT, L"VFX Engine"))
    {
        std::cout << "[Error] Window creation failed" << std::endl;
        return false;
    }
    std::cout << "[OK] Window created" << std::endl;

    // Graphics
    if (!m_Graphics.Initialize(m_Window.GetHandle(), WINDOW_WIDTH, WINDOW_HEIGHT))
    {
        std::cout << "[Error] Graphics initialization failed" << std::endl;
        return false;
    }

    // Renderer
    if (!m_Renderer.Initialize(m_Graphics.GetDevice(), m_Graphics.GetContext()))
    {
        std::cout << "[Error] Renderer initialization failed" << std::endl;
        return false;
    }
	// ImGui
    if (!DebugManager::Get().Initialize(
        m_Window.GetHandle(),
        m_Graphics.GetDevice(),
        m_Graphics.GetContext(),
        &m_Timer,
        &m_Renderer))
    {
        std::cout << "[Error] ImGui initialization failed" << std::endl;
        return false;
    }
	ResourceManager::Get().Initialize(m_Graphics.GetDevice());
    InputManager::Get().Initialize(m_Window.GetHandle());
	if(!m_Game.Initialize(&m_Renderer)) return false;
    // Timer
    m_Timer.Start();

    std::cout << "[OK] Application initialized" << std::endl;
    m_IsRunning = true;
    return true;
}

void Application::Run()
{
    while (m_IsRunning && m_Window.ProcessMessage())
    {
        m_Timer.Tick();
        float dt = m_Timer.DeltaTime();
		//ImGuiフレーム開始
        InputManager::Get().Update();
        DebugManager::Get().Update(dt);
        DebugManager::Get().BeginFrame();
		// Update
		m_Game.Update(dt);
		// Render
        m_Graphics.BeginFrame();    
		m_Game.Render();
        DebugManager::Get().Render();    // Grid/座標軸
		//ImGuiフレーム終了
        DebugManager::Get().EndFrame();
        m_Graphics.EndFrame();
    }
}

void Application::Shutdown()
{
    DebugManager::Get().Shutdown();
    ResourceManager::Get().Shutdown();
    m_Renderer.Shutdown();
    s_Instance = nullptr;
    std::cout << "[OK] Shutdown complete" << std::endl;
}