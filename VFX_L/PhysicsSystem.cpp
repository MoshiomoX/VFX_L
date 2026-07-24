// ============================================================
// PhysicsSystem.cpp
// ============================================================
#include "PhysicsSystem.h"
#include "TransformComponent.h"
#include "ColliderComponent.h"
#include "RigidbodyComponent.h"
#include "CollisionSystem.h"
#include "CollisionMath.h"
#include "PhysicsMath.h"
#include "View.h"

using DirectX::SimpleMath::Vector3;

// 1つの動的 Entity を、全静的 collider に対して押し出す（分軸の1軸ぶん）
// 戻り値: 接地したか（上向き法線に当たったか）
static bool ResolveAgainstStatics(
    Registry& reg, Entity self,
    TransformComponent& tf, ColliderComponent& col, RigidbodyComponent& rb,
    CollisionSystem& collision)
{
    using namespace CollisionMath;
    bool grounded = false;

    // 自分のワールド形状（今は Capsule 前提。Sphere も可）
    Vector3 selfCenter = tf.position + col.offset;

    // 全 collider を走査し、静的なものと判定・押し出し
    for (const auto& wc : collision.GetWorldColliders())
    {
        if (wc.entity == self) continue;

        // 相手が静的地形か？（Rigidbody を持ち isStatic、または Rigidbody 無し=静的扱い）
        bool otherStatic = true;
        if (reg.Has<RigidbodyComponent>(wc.entity))
            otherStatic = reg.Get<RigidbodyComponent>(wc.entity).isStatic;
        if (!otherStatic) continue;   // 動的同士は今は無視

        // 自分 Capsule vs 相手 AABB の Contact を取る
        Contact contact;
        bool hit = false;

        if (col.shape == ColliderShape::Capsule && wc.shape == ColliderShape::AABB)
        {
            Capsule selfCap{ selfCenter, col.radius, col.height };
            AABB otherBox{ wc.center - wc.halfExtents, wc.center + wc.halfExtents };
            hit = IntersectCapsuleAABB(selfCap, otherBox, contact);
        }
        // 他の形状組み合わせは必要になったら追加

        if (hit)
        {
            // push-out: 位置を法線方向に depth ぶん動かす
            tf.position += PhysicsMath::ResolvePenetration(contact.normal, contact.depth);
            selfCenter = tf.position + col.offset;   // 更新

            // 速度応答
            if (rb.response == ResponseMode::Slide)
                rb.velocity = PhysicsMath::SlideVelocity(rb.velocity, contact.normal);
            else if (rb.response == ResponseMode::Bounce)
                rb.velocity = PhysicsMath::ReflectVelocity(rb.velocity, contact.normal, rb.restitution);
            else
                rb.velocity = Vector3(0, 0, 0);

            // 上向き法線に当たった = 接地
            if (contact.normal.y > 0.5f)
                grounded = true;
        }
    }
    return grounded;
}

void PhysicsSystem::Update(Registry& reg, float dt, CollisionSystem& collision)
{
    // 動的 Rigidbody を移動 + 応答
    reg.CreateView<TransformComponent, ColliderComponent, RigidbodyComponent>()
        .Each([&](Entity e, TransformComponent& tf, ColliderComponent& col, RigidbodyComponent& rb)
            {
                if (rb.isStatic) return;   // 静的は動かさない

                // --- 1) 重力 ---
                if (rb.useGravity)
                    rb.velocity.y += m_Gravity * dt;

                // --- 2) 移動を適用 ---
                tf.position += rb.velocity * dt;

                // --- 3) 衝突応答（最新の collider 配列で押し出し）---
                //     ※ collision.Update は PhysicsSystem より前に呼ばれている前提
                rb.isGrounded = ResolveAgainstStatics(reg, e, tf, col, rb, collision);
            });
}