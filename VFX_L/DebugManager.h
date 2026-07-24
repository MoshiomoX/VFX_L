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
      // --- デバッグ形状描画（線框）---
    void DrawWireSphere(const Vector3& center, float radius, const Color& color);
    void DrawWireCapsule(const Vector3& center, float radius, float height, const Color& color);
    void DrawWireAABB(const Vector3& center, const Vector3& halfExtents, const Color& color);

    void DrawRay(const Vector3& origin, const Vector3& dir, float length, const Color& color);
    void AddDebugLine(const Vector3& start, const Vector3& end, const Color& color);
    // レイキャスト結果の可視化（命中まで線 + 命中点マーカー + 法線）
    // hit=false なら maxDist まで薄い色で描く
    void DrawRaycast(const Vector3& origin, const Vector3& dir, float maxDist,
        bool hit, float hitT, const Vector3& hitPoint, const Vector3& hitNormal,
        const Color& hitColor, const Color& missColor);
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
	bool m_UseDebugCamera = false;

    bool m_ShowGrid = true;
    float m_GridSize = 50.0f;
    float m_GridStep = 1.0f;
    float m_AxisLength = 3.0f;
};