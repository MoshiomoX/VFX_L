// ============================================================
// InteractionSystem.cpp
// ============================================================
#include "ECS/System/InteractionSystem.h"
#include "ECS/Registry.h"
#include "ECS/View.h"
#include "Component/TransformComponent.h"
#include "Component/InteractableComponent.h"
#include "Graphics/Light/PointLightManager.h"
#include <cfloat>
#include <cmath>

using DirectX::SimpleMath::Vector3;

Entity InteractionSystem::Update(Registry& reg, Entity player, float dt, bool interactPressed)
{
    m_Time += dt;
    ClearFocus();

    const bool hasPlayer = reg.IsValid(player) && reg.Has<TransformComponent>(player);
    const Vector3 playerPos = hasPlayer ? reg.Get<TransformComponent>(player).position : Vector3::Zero;

    float bestDistSq = FLT_MAX;
    reg.CreateView<TransformComponent, InteractableComponent>()
        .Each([&](Entity e, TransformComponent& tf, InteractableComponent& it)
            {
                // ---- 浮遊と回転（下端が地面になるよう 0..bobHeight で上下）----
                if (it.animate)
                {
                    const float bob = (std::sin(m_Time * bobSpeed + it.phase) * 0.5f + 0.5f) * bobHeight;
                    tf.position = it.basePos + Vector3(0.0f, bob, 0.0f);
                    tf.rotation.y = std::fmod(tf.rotation.y + spinSpeed * dt, 360.0f);
                }

                // ---- 一番近い使える物（水平距離。高さは段差で多少ずれても使えるように無視）----
                if (!hasPlayer) return;
                const float dx = it.basePos.x - playerPos.x;
                const float dz = it.basePos.z - playerPos.z;
                const float dSq = dx * dx + dz * dz;
                if (dSq <= it.radius * it.radius && dSq < bestDistSq)
                {
                    bestDistSq = dSq;
                    m_Focus = e;
                    m_Prompt = it.prompt;
                }
            });

    return (interactPressed && HasFocus()) ? m_Focus : EntityTraits::NULL_ENTITY;
}

void InteractionSystem::SubmitLights(Registry& reg)
{
    auto& lights = PointLightManager::Get();
    reg.CreateView<InteractableComponent>()
        .Each([&](Entity e, InteractableComponent& it)
            {
                // 近くに寄っている物は明るく（どれが反応しているか分かるように。focus は前フレームの物）
                const float gain = (e == m_Focus) ? 1.5f : 1.0f;
                lights.Add(it.basePos + Vector3(0.0f, it.lightHeight, 0.0f),
                    it.lightColor, it.lightRadius, it.lightIntensity * gain);
            });
}
