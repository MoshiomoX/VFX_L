#pragma once
#include "ObjectManager.h"
#include "CameraBase.h"

class Renderer;

class SceneBase
{
public:
    virtual ~SceneBase() = default;

    virtual void Init(){}
    virtual void Shutdown() {}
    virtual void Update(float dt);
    virtual void Render(Renderer& renderer);

    // Objectä«óù
    GameObject* CreateObject();
    void DestroyObject(GameObject* obj);

    // Camera
    void SetCamera(CameraBase* camera) { m_Camera = camera; }
    CameraBase* GetCamera() const { return m_Camera; }

protected:
    ObjectManager m_ObjectManager;
    CameraBase* m_Camera = nullptr;
};