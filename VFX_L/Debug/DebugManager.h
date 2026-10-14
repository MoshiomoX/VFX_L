#pragma once
#include <memory>
#include <d3d11.h>
#include "Debug/ImGuiRenderer.h"
#include "Camera/DebugCamera.h"
#include "Debug/DebugLineRenderer.h"
#include "Scene/SceneType.h"


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

    // y = 0 の参照格子（原点 ±GridSize、1m 刻み）と座標軸。エディタ向け。
    // 戦闘シーンは地面が y = 0 にあって格子と深度で競り合い、地面に縞が出るので入る時に消す（2026-10-03）
    bool GetShowGrid() const { return m_ShowGrid; }
    void SetShowGrid(bool show) { m_ShowGrid = show; }

    // シーン切替を依頼する（実行は次の SceneManager::Update）。
    // デバッグカメラは旧シーンのカメラを指しているので、切替前に必ず解除する
    void RequestScene(SceneType type);
      // --- デバッグ形状描画（ワイヤーフレーム）---
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