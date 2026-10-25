#include "Core/Application.h"
#include "Graphics/Light/PointLightManager.h"
#include "Debug/DebugManager.h"
#include "Debug/FrameProfiler.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "Manager/InputManager.h"
#include "Audio/AudioSystem.h"
#include "Core/GameSettings.h"
#include "Core/GameProgress.h"
#include "UI/UIDeco.h"
#include "Graphics/PostProcess/SceneSnapshot.h"
#include "UI/InputGlyph.h"
#include "Core/VideoCapture.h"


Application* Application::s_Instance = nullptr;

bool Application::Initialize()
{
    s_Instance = this;

    // Window（動画の書き出し中は VFXL_CAPTURE_W / H、既定 1920×1080。Core/VideoCapture）
    const VideoCapture& capture = VideoCapture::Get();
    if (!m_Window.Create(capture.Enabled() ? capture.Width() : 1600, capture.Enabled() ? capture.Height() : 900, L"VFX Engine"))
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
    AudioSystem::Get().Initialize(capture.Enabled());   // 音（装置が無くても続ける。書き出し中は装置を開かない）
    if (capture.Enabled() && !capture.CaptureMusic())
        AudioSystem::Get().SetVolume(AudioSystem::Bus::Music, 0.0f);   // BGM は編集で 1 本通して乗せる（保存はしない）
    GameSettings::Get().Load();        // プレイヤーの設定（敵味方の縁取りの ON / OFF。無ければ既定値）
    GameProgress::Get().Load();        // 進み具合（開いたマップ・クリアした Tier。無ければ草原 Tier 1 だけ。2026-10-21）
    {
        char env[16] = {};   // TEMP-TEST: VFXL_UI_SCALE=1.25 など、UI 全体の倍率を起動時に変える（見比べ用。既定 1.5）
        if (GetEnvironmentVariableA("VFXL_UI_SCALE", env, sizeof(env)) > 0 && env[0])
            UIDeco::SetUIScale((float)atof(env));
    }
	if (!m_Game.Initialize(&m_Renderer))
    {
        // 先読みのスレッドが動いている：join せずに戻ると ResourceManager の静的な破棄で std::terminate になる。
        // device（m_Graphics）が生きている今のうちに待って片付ける
        m_Game.Shutdown();
        ResourceManager::Get().Shutdown();
        return false;
    }
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
        if (VideoCapture::Get().Enabled())
            dt = VideoCapture::Get().FrameDt() * VideoCapture::Get().TimeScale();   // 書き出し中は 1/60 秒ずつ（重くてもコマ落ちしない。スローは TimeScale）
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
            VideoCapture::Get().PumpAudio();  // 書き出し中：このフレームの 1/60 秒ぶんの音を読み出す
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
    m_Game.Shutdown();                 // 今のシーンを先に片付ける（音・ImGui・ResourceManager・Renderer がまだ生きている内に）
    VideoCapture::Get().Finish();      // 書き出し中なら閉じる（ffmpeg の終了を待つ）
    SceneSnapshot::Get().Shutdown();   // リザルトの背景の写真（シーンをまたいで持つ。デバイスより先に放す）
    InputGlyph::Shutdown();            // パッドのボタンの絵（同上）
    AudioSystem::Get().Shutdown();
    FrameProfiler::Get().ShutdownGpu();
    DebugManager::Get().Shutdown();
    ResourceManager::Get().Shutdown();
    m_Renderer.Shutdown();
    s_Instance = nullptr;
    std::cout << "[OK] Shutdown complete" << std::endl;
}