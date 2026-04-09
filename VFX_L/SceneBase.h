#pragma once
#include "ObjectManager.h"
#include "Camera.h"

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
    void SetCamera(Camera* camera) { m_Camera = camera; }
    Camera* GetCamera() const { return m_Camera; }

protected:
    ObjectManager m_ObjectManager;
    Camera* m_Camera = nullptr;
};