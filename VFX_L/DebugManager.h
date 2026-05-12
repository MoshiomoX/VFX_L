#pragma once
#include <memory>
#include <d3d11.h>
#include "ImGuiRenderer.h"
#include "DebugCamera.h"
#include "DebugLineRenderer.h"


class EngineTimer;
class Renderer;

class DebugManager
{
public:
    static DebugManager& Get()
    {
        static DebugManager instance;
        return instance;
    }

    bool Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context, EngineTimer* timer, Renderer* renderer);
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    void Update(float dt);
    void Render();
    CameraBase* GetActiveCamera();
    void SetUseDebugCamera(bool use);  
    bool IsUsingDebugCamera() const { return m_UseDebugCamera; }
private:
    DebugManager() = default;
    ~DebugManager();
    DebugManager(const DebugManager&) = delete;
    DebugManager& operator=(const DebugManager&) = delete;

private:
    std::unique_ptr<ImguiRenderer> m_ImguiRenderer;
    EngineTimer* m_Timer = nullptr;
	Renderer* m_Renderer = nullptr;
    DebugCamera m_DebugCamera;
    DebugLineRenderer m_LineRenderer;
    CameraBase* m_PreviousCamera = nullptr;

    bool m_Initialized = false;
	bool m_UseDebugCamera = true;

    bool m_ShowGrid = true;
    float m_GridSize = 50.0f;
    float m_GridStep = 1.0f;
    float m_AxisLength = 3.0f;
};