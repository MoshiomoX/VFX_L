// ============================================================
// SceneBase.h
// 場面の基底。カメラを持ち、Render の頭で点光源表を GPU へ上げる。
// （旧 GameObject / ObjectManager 経由の描画は 2026-09-30 に削除。実体は全部 ECS の Registry）
// ============================================================
#pragma once
#include "Camera/CameraBase.h"

class Renderer;

class SceneBase
{
public:
    virtual ~SceneBase() = default;

    virtual void Init(){}
    virtual void Shutdown() {}
    virtual void Update(float dt);
    virtual void Render(Renderer& renderer);

    // Camera
    void SetCamera(CameraBase* camera) { m_Camera = camera; }
    CameraBase* GetCamera() const { return m_Camera; }

protected:
    CameraBase* m_Camera = nullptr;
};
