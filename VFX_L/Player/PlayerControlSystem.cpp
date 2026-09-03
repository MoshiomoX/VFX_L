// ============================================================
// PlayerControlSystem.cpp
// ============================================================
#include "Player/PlayerControlSystem.h"
#include "ECS/Registry.h"
#include "Component/TransformComponent.h"
#include "Player/PlayerStatsComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Player/PlayerTag.h"
#include "Manager/InputMap.h"
#include "ECS/View.h"
#include "Component/WandComponent.h"
#include "Debug/DebugManager.h"
#include "ImGui.h"
void PlayerControlSystem::Update(Registry& reg, float dt, CameraBase* camera)
{
 
    const bool blocked = DebugManager::Get().IsUsingDebugCamera()
        || ImGui::GetIO().WantCaptureKeyboard;

    const bool castTrigger = blocked ? false : InputMap::GetCastTrigger();

    reg.CreateView<WandComponent, PlayerTag>()
        .Each([&](Entity e, WandComponent& wand, PlayerTag&)
            {
            
                wand.castRequested = castTrigger;
            });

    if (blocked) return;
    if (!camera) return;

    auto move = InputMap::GetMoveInput();

    Vector3 camF = camera->GetForward(); camF.y = 0; camF.Normalize();
    Vector3 camR = camera->GetRight();   camR.y = 0; camR.Normalize();

    Vector3 dir = camR * move.x + camF * move.y;
    reg.CreateView<TransformComponent, RigidbodyComponent,
        PlayerStatsComponent, PlayerTag>()
        .Each([&](Entity e, TransformComponent& tf, RigidbodyComponent& rb,
            PlayerStatsComponent& stats, PlayerTag&)
            {
                // 能力値は Entity が持つ。System は値を持たない
                rb.velocity.x = dir.x * stats.moveSpeed;
                rb.velocity.z = dir.z * stats.moveSpeed;
                tf.rotation.y = DirectX::XMConvertToDegrees(std::atan2(camF.x, camF.z));

                if (rb.isGrounded && InputMap::GetJumpTrigger())
                {
                    rb.velocity.y = stats.jumpPower;
                    rb.isGrounded = false;
                }
            });
}