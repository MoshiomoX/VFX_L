#include "SceneBase.h"
#include "Renderer.h"
#include "MeshRenderer.h"
#include "ModelRenderer.h"

void SceneBase::Update(float dt)
{
    m_ObjectManager.Update(dt);
}

void SceneBase::Render(Renderer& renderer)
{
    if (m_Camera)
    {
        renderer.SetCamera(m_Camera);
    }

    for (auto& obj : m_ObjectManager.GetObjects())
    {
        auto* mr = obj->GetComponent<MeshRenderer>();
        if (mr)
        {
            mr->Draw(renderer);
        }
        auto* modelR = obj->GetComponent<ModelRenderer>();
        if (modelR)
        {
            modelR->Draw(renderer);
        }
    }
}

GameObject* SceneBase::CreateObject()
{
    return m_ObjectManager.CreateObject();
}

void SceneBase::DestroyObject(GameObject* obj)
{
    m_ObjectManager.DestroyObject(obj);
}