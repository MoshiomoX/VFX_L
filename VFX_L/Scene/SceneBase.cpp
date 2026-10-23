#include "Scene/SceneBase.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Light/PointLightManager.h"

void SceneBase::Update(float)
{
}

void SceneBase::Render(Renderer& renderer)
{
    if (m_Camera)
    {
        renderer.SetCamera(m_Camera);
    }

    // 点光源リストを GPU へ（Update 中に SwarmSystem が先に上げていれば何もしない）
    PointLightManager::Get().Upload(renderer.GetContext());
    // 表がそろったので、このカメラでクラスタへ振り分ける（Forward+、2026-10-23）。
    // カメラの無い場面は作らない = PS は前のとおり全部の光を回す
    if (m_Camera)
        PointLightManager::Get().BuildClusters(renderer.GetContext(), *m_Camera);
}
