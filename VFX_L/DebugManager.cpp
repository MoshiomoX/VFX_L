#include "DebugManager.h"
#include "ImGuiRenderer.h"
#include "EngineTimer.h"
#include "Application.h"
#include "imgui.h"
#include "Renderer.h"
#include <iostream>


DebugManager::~DebugManager() = default;

bool DebugManager::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context, EngineTimer* timer, Renderer* renderer)
{
    m_Timer = timer;
    m_Renderer = renderer;
    m_Renderer->SetCamera(&m_DebugCamera);
    m_DebugCamera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 1000.0f);
    m_DebugCamera.LookAt({ 0.0f, 5.0f, -15.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

    m_ImguiRenderer = std::make_unique<ImguiRenderer>();
    if (!m_ImguiRenderer->Initialize(hwnd, device, context))
    {
        std::cout << "[Error] ImguiRenderer init failed" << std::endl;
        return false;
    }
    if (!m_LineRenderer.Initialize(device, context))
    {
        std::cout << "[Error] DebugLineRenderer init failed" << std::endl;
        return false;
    }
    m_Initialized = true;
    std::cout << "[OK] DebugManager initialized" << std::endl;
    return true;
}

void DebugManager::Shutdown()
{
    if (!m_Initialized) return;

    m_ImguiRenderer->Shutdown();
    m_ImguiRenderer.reset();
    m_Timer = nullptr;

    m_Initialized = false;
    std::cout << "[OK] DebugManager shutdown" << std::endl;
}

void DebugManager::BeginFrame()
{
    if (!m_Initialized) return;
    m_ImguiRenderer->BeginFrame();

    ImGui::Begin("Debug Info");
    ImGui::Text("FPS: %.1f", m_Timer->GetFPS());
    ImGui::Text("Delta: %.4f ms", m_Timer->DeltaTime() * 1000.0f);

    // DebugCamera切替
	bool useDebug = m_UseDebugCamera;
    if (ImGui::Checkbox("Debug Camera", &useDebug))
    {
        SetUseDebugCamera(useDebug);
    }
    if (m_UseDebugCamera)
    {
        ImGui::Text("Alt+LMB: Orbit  Alt+MMB: Track  Alt+RMB: Dolly");
        ImGui::Text("RMB+WASD: Flight  Q/E: Up/Down");
        ImGui::SliderFloat("Flight Speed", &m_DebugCamera.flightSpeed, 0.01f, 1.0f);
    }


    ImGui::Separator();
    ImGui::Checkbox("Show Grid", &m_ShowGrid);
    if (m_ShowGrid)
    {
        ImGui::SliderFloat("Grid Size", &m_GridSize, 10.0f, 200.0f);
      //  ImGui::SliderFloat("Grid Step", &m_GridStep, 0.5f, 10.0f);
        ImGui::SliderFloat("Axis Length", &m_AxisLength, 1.0f, 20.0f);
    }

    ImGui::End();
}

void DebugManager::EndFrame()
{
    if (!m_Initialized) return;
    m_ImguiRenderer->EndFrame();
}

void DebugManager::Update(float dt)
{
    if (!m_Initialized) return;
    if (m_UseDebugCamera)
    {
        m_DebugCamera.Update(dt);
        m_Renderer->SetCamera(&m_DebugCamera);

        // 当前シーンのCameraも更新
        auto* scene = Application::Get().GetGame().GetSceneManager().GetCurrentScene();
        if (scene) scene->SetCamera(&m_DebugCamera);
    }
}

CameraBase* DebugManager::GetActiveCamera()
{
    if (m_UseDebugCamera)
        return &m_DebugCamera;
    return nullptr;
}


void DebugManager::Render()
{
    if (!m_Initialized) return;

    m_LineRenderer.SetCamera(
        m_UseDebugCamera ? &m_DebugCamera : m_PreviousCamera);

    if (m_ShowGrid)
    {
        // 座標軸
        m_LineRenderer.AddLine({ 0,0,0 }, { m_AxisLength,0,0 }, Color(1, 0, 0, 1));
        m_LineRenderer.AddLine({ 0,0,0 }, { 0,m_AxisLength,0 }, Color(0, 1, 0, 1));
        m_LineRenderer.AddLine({ 0,0,0 }, { 0,0,m_AxisLength }, Color(0, 0, 1, 1));

        // Grid
        Color gridColor(0.4f, 0.4f, 0.4f, 1.0f);
        for (float i = -m_GridSize; i <= m_GridSize; i += m_GridStep)
        {
            m_LineRenderer.AddLine({ i, 0, -m_GridSize }, { i, 0, m_GridSize }, gridColor);
            m_LineRenderer.AddLine({ -m_GridSize, 0, i }, { m_GridSize, 0, i }, gridColor);
        }
    }

    m_LineRenderer.Render();
}


void DebugManager::SetUseDebugCamera(bool use)
{
    auto* scene = Application::Get().GetGame().GetSceneManager().GetCurrentScene();

    if (use && !m_UseDebugCamera)
    {
        // 切り替え前のCameraを保存
        if (scene) m_PreviousCamera = scene->GetCamera();
        m_Renderer->SetCamera(&m_DebugCamera);
        if (scene) scene->SetCamera(&m_DebugCamera);
    }
    else if (!use && m_UseDebugCamera)
    {
        // 元のCameraに復元
        m_Renderer->SetCamera(m_PreviousCamera);
        if (scene) scene->SetCamera(m_PreviousCamera);
        m_PreviousCamera = nullptr;
    }
    m_UseDebugCamera = use;
}