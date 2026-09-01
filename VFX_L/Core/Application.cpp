#include "Core/Application.h"
#include "Scene/TestScene.h"
#include "Debug/DebugManager.h"
#include <iostream>
#include "Manager/ResourceManager.h"
#include "Manager/InputManager.h"


Application* Application::s_Instance = nullptr;

bool Application::Initialize()
{
    s_Instance = this;

    // Window
    if (!m_Window.Create(1600, 900, L"VFX Engine"))
    {
        std::cout << "[Error] Window creation failed" << std::endl;
        return false;
    }
    std::cout << "[OK] Window created" << std::endl;

    // Graphics
    if (!m_Graphics.Initialize(m_Window.GetHandle(),
        m_Window.GetWidth(),
        m_Window.GetHeight()))
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
        if (m_Window.ConsumeResizeFlag())
            m_Graphics.Resize(m_Window.GetWidth(),
                m_Window.GetHeight());
		//ImGuiフレーム開始
        InputManager::Get().Update();
        DebugManager::Get().Update(dt);
        DebugManager::Get().BeginFrame();
		// Update
		m_Game.Update(dt);
		// Render
        m_Graphics.BeginFrame();    
        DebugManager::Get().Render();    // Grid/座標軸
		m_Game.Render();
		//ImGuiフレーム終了
        DebugManager::Get().EndFrame();

        m_Graphics.RestoreRenderTarget();
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