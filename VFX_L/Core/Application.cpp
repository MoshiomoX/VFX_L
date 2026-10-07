#include "Core/Application.h"
#include "Graphics/Light/PointLightManager.h"
#include "Debug/DebugManager.h"
#include "Debug/FrameProfiler.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "Manager/InputManager.h"
#include "Audio/AudioSystem.h"
#include "Core/GameSettings.h"
#include "UI/UIDeco.h"


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
    FrameProfiler::Get().InitGpu(m_Graphics.GetDevice(), m_Graphics.GetContext());   // PROFILE_SCOPE_GPU の timestamp
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
    // preload skinned models on worker threads (Res::Mdl::kPreload). Scenes that need them wait on the future
    ResourceManager::Get().PreloadModelsAsync(std::vector<std::string>(std::begin(Res::Mdl::kPreload), std::end(Res::Mdl::kPreload)));
    InputManager::Get().Initialize(m_Window.GetHandle());
    AudioSystem::Get().Initialize();   // 音（装置が無くても続ける）
    GameSettings::Get().Load();        // プレイヤーの設定（アウトラインの ON / OFF。無ければ既定値）
    {
        char env[16] = {};   // TEMP-TEST: VFXL_UI_SCALE=1.25 など、UI 全体の倍率を起動時に変える（見比べ用。既定 1.5）
        if (GetEnvironmentVariableA("VFXL_UI_SCALE", env, sizeof(env)) > 0 && env[0])
            UIDeco::SetUIScale((float)atof(env));
    }
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
        // 各段の CPU / GPU 時間は FrameProfiler（シーンのパネルの「Frame Profiler」、perf / stress 自己テストのログ）
        FrameProfiler::Get().BeginFrame();
        {
            PROFILE_SCOPE("Input + ImGui NewFrame");
            InputManager::Get().Update();
            DebugManager::Get().Update(dt);
            DebugManager::Get().BeginFrame();
            PointLightManager::Get().BeginFrame();   // point light list: clear per frame
        }
        {
            PROFILE_SCOPE_GPU("Update");   // GPU = 雑魚・弾・粒子の compute
            m_Game.Update(dt);
            AudioSystem::Get().Update(dt);   // 鳴り終わった声の回収・BGM のフェード
        }
        {
            PROFILE_SCOPE_GPU("Render");
            m_Graphics.BeginFrame();
            DebugManager::Get().Render();
            m_Game.Render();
        }
        {
            PROFILE_SCOPE_GPU("Resolve + bloom + ImGui draw");
            m_Graphics.BeginUI();
            DebugManager::Get().EndFrame();
        }
        {
            PROFILE_SCOPE("Present");
            m_Graphics.EndFrame();
        }
        FrameProfiler::Get().EndFrame();
    }
}

void Application::Shutdown()
{
    AudioSystem::Get().Shutdown();
    FrameProfiler::Get().ShutdownGpu();
    DebugManager::Get().Shutdown();
    ResourceManager::Get().Shutdown();
    m_Renderer.Shutdown();
    s_Instance = nullptr;
    std::cout << "[OK] Shutdown complete" << std::endl;
}