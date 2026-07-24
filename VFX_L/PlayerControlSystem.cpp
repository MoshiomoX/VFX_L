// ============================================================
// PlayerControlSystem.cpp
// ============================================================
#include "PlayerControlSystem.h"
#include "Registry.h"
#include "TransformComponent.h"
#include "RigidbodyComponent.h"
#include "PlayerTag.h"
#include "InputMap.h"
#include "View.h"
#include"DebugManager.h"
#include"ImGui.h"
void PlayerControlSystem::Update(Registry& reg, float dt, CameraBase* camera)
{
    if (DebugManager::Get().IsUsingDebugCamera()) return;
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (!camera) return;

    auto move = InputMap::GetMoveInput();

    // カメラ基準の水平方向を求める（Y成分を落として水平化）
    Vector3 camF = camera->GetForward(); camF.y = 0; camF.Normalize();
    Vector3 camR = camera->GetRight();   camR.y = 0; camR.Normalize();

    // 入力をカメラ基準のワールド方向へ変換
    Vector3 dir = camR * move.x + camF * move.y;

    reg.CreateView<TransformComponent, RigidbodyComponent, PlayerTag>()
        .Each([&](Entity e, TransformComponent& tf, RigidbodyComponent& rb, PlayerTag&)
            {
                // 水平移動
                rb.velocity.x = dir.x * m_MoveSpeed;
                rb.velocity.z = dir.z * m_MoveSpeed;
                tf.rotation.y = DirectX::XMConvertToDegrees(std::atan2(camF.x, camF.z));
                // ジャンプ：接地中のみ、上方向へ初速度を与える
                if (rb.isGrounded && InputMap::GetJumpTrigger())
                {
                    rb.velocity.y = m_JumpPower;
                    rb.isGrounded = false;   // 即座に空中扱いにして二重ジャンプを防ぐ
                }
            });
}
