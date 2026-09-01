// ============================================================
// DebugManager.cpp
// ============================================================
#include "Debug/DebugManager.h"
#include "Debug/ImGuiRenderer.h"
#include "Core/Timer/EngineTimer.h"
#include "Core/Application.h"
#include "imgui.h"
#include "Graphics/Renderer/Renderer.h"
#include <cmath>
#include <iostream>

DebugManager::~DebugManager() = default;

bool DebugManager::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context,
    EngineTimer* timer, Renderer* renderer)
{
    m_Timer = timer;
    m_Renderer = renderer;

    m_DebugCamera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 10000.0f);
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

    // ※旧: ここで m_PreviousCamera を補完していたが、
    //   その時点で scene->GetCamera() は既にデバッグカメラに差し替わっており、
    //   バックアップがデバッグカメラ自身になって復元不能になるため削除した。
    //   バックアップは SetUseDebugCamera() 側でのみ行う。

    if (m_UseDebugCamera)
    {
        m_DebugCamera.Update(dt);
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

    // --- 線描画に使うカメラを決める ---
    // デバッグカメラON → デバッグカメラ
    // OFF → 現在のシーンのカメラ
    // ※ m_PreviousCamera は「切替前のバックアップ」であり通常時は nullptr。
    //   これを使っていたため、デバッグカメラを使わないと線が出ない不具合になっていた。
    CameraBase* cam = nullptr;
    if (m_UseDebugCamera)
    {
        cam = &m_DebugCamera;
    }
    else
    {
        auto* scene = Application::Get().GetGame().GetSceneManager().GetCurrentScene();
        if (scene) cam = scene->GetCamera();
    }
    m_LineRenderer.SetCamera(cam);

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
        // 切り替え前のカメラを保存してからデバッグカメラへ
        if (scene) m_PreviousCamera = scene->GetCamera();
        m_Renderer->SetCamera(&m_DebugCamera);
        if (scene) scene->SetCamera(&m_DebugCamera);
    }
    else if (!use && m_UseDebugCamera)
    {
        // 元のカメラに復元
        m_Renderer->SetCamera(m_PreviousCamera);
        if (scene) scene->SetCamera(m_PreviousCamera);
        m_PreviousCamera = nullptr;
    }
    m_UseDebugCamera = use;
}

// ============================================================
// デバッグ形状描画（線框）
// ============================================================
void DebugManager::AddDebugLine(const Vector3& start, const Vector3& end, const Color& color)
{
    m_LineRenderer.AddLine(start, end, color);
}

void DebugManager::DrawWireSphere(const Vector3& center, float radius, const Color& color)
{
    const int SEG = 24;
    const float TAU = 6.28318530718f;

    Vector3 prevXY, prevYZ, prevXZ;
    for (int i = 0; i <= SEG; ++i)
    {
        float a = TAU * i / SEG;
        float c = std::cos(a) * radius;
        float s = std::sin(a) * radius;

        Vector3 curXY = center + Vector3(c, s, 0);
        Vector3 curYZ = center + Vector3(0, c, s);
        Vector3 curXZ = center + Vector3(c, 0, s);

        if (i > 0)
        {
            AddDebugLine(prevXY, curXY, color);
            AddDebugLine(prevYZ, curYZ, color);
            AddDebugLine(prevXZ, curXZ, color);
        }
        prevXY = curXY; prevYZ = curYZ; prevXZ = curXZ;
    }
}

void DebugManager::DrawWireCapsule(const Vector3& center, float radius, float height,
    const Color& color)
{
    float halfH = height * 0.5f;
    Vector3 top = center + Vector3(0, halfH, 0);
    Vector3 bottom = center + Vector3(0, -halfH, 0);

    const int SEG = 24;
    const float TAU = 6.28318530718f;

    // 上下の水平円
    Vector3 prevT, prevB;
    for (int i = 0; i <= SEG; ++i)
    {
        float a = TAU * i / SEG;
        float c = std::cos(a) * radius;
        float s = std::sin(a) * radius;
        Vector3 curT = top + Vector3(c, 0, s);
        Vector3 curB = bottom + Vector3(c, 0, s);
        if (i > 0)
        {
            AddDebugLine(prevT, curT, color);
            AddDebugLine(prevB, curB, color);
        }
        prevT = curT; prevB = curB;
    }

    // 側面の縦線（4本）
    for (int i = 0; i < 4; ++i)
    {
        float a = TAU * i / 4;
        Vector3 off(std::cos(a) * radius, 0, std::sin(a) * radius);
        AddDebugLine(bottom + off, top + off, color);
    }

    // 上下の半球（縦断面2枚）
    for (int axis = 0; axis < 2; ++axis)
    {
        Vector3 prevTop, prevBot;
        for (int i = 0; i <= SEG / 2; ++i)
        {
            float a = 3.14159265f * i / (SEG / 2);
            float y = std::sin(a) * radius;
            float x = std::cos(a) * radius;
            Vector3 dir = (axis == 0) ? Vector3(x, 0, 0) : Vector3(0, 0, x);
            Vector3 curTop = top + Vector3(dir.x, y, dir.z);
            Vector3 curBot = bottom + Vector3(dir.x, -y, dir.z);
            if (i > 0)
            {
                AddDebugLine(prevTop, curTop, color);
                AddDebugLine(prevBot, curBot, color);
            }
            prevTop = curTop; prevBot = curBot;
        }
    }
}

void DebugManager::DrawWireAABB(const Vector3& center, const Vector3& halfExtents,
    const Color& color)
{
    Vector3 mn = center - halfExtents;
    Vector3 mx = center + halfExtents;

    Vector3 v[8] = {
        { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z },
        { mx.x, mn.y, mx.z }, { mn.x, mn.y, mx.z },
        { mn.x, mx.y, mn.z }, { mx.x, mx.y, mn.z },
        { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z },
    };
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7},
    };
    for (auto& e : edges)
        AddDebugLine(v[e[0]], v[e[1]], color);
}

// ============================================================
// レイの可視化
// ============================================================
void DebugManager::DrawRay(const Vector3& origin, const Vector3& dir,
    float length, const Color& color)
{
    AddDebugLine(origin, origin + dir * length, color);
}

void DebugManager::DrawRaycast(const Vector3& origin, const Vector3& dir, float maxDist,
    bool hit, float hitT, const Vector3& hitPoint, const Vector3& hitNormal,
    const Color& hitColor, const Color& missColor)
{
    if (hit)
    {
        AddDebugLine(origin, hitPoint, hitColor);

        // 命中点の十字マーカー
        const float m = 0.15f;
        AddDebugLine(hitPoint - Vector3(m, 0, 0), hitPoint + Vector3(m, 0, 0), hitColor);
        AddDebugLine(hitPoint - Vector3(0, m, 0), hitPoint + Vector3(0, m, 0), hitColor);
        AddDebugLine(hitPoint - Vector3(0, 0, m), hitPoint + Vector3(0, 0, m), hitColor);

        // 命中面の法線（黄）
        AddDebugLine(hitPoint, hitPoint + hitNormal * 0.5f, Color(1, 1, 0, 1));
    }
    else
    {
        AddDebugLine(origin, origin + dir * maxDist, missColor);
    }
}