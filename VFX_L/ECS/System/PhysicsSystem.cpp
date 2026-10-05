// ============================================================
// PhysicsSystem.cpp
// ============================================================
#include "ECS/System/PhysicsSystem.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Collider/CollisionSystem.h"
#include "Collider/CollisionMath.h"
#include "Component/PhysicsMath.h"
#include "ECS/View.h"

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
    Vector3 groundNormal(0.0f, 1.0f, 0.0f);   // 床扱いの接触のうち一番傾いた物（坂の途中で平地の箱にも触れていても坂を取る）

    // 自分のワールド形状（今は Capsule 前提。Sphere も可）
    Vector3 selfCenter = tf.position + col.offset;

    // 近くの静的な collider だけを判定・押し出し（全件は回さない。押し出しで動く分 + 1m の余白）
    static std::vector<int> s_Near;   // 使い回す（毎フレームの確保を避ける）
    collision.GatherStaticNear(selfCenter, col.radius + col.height * 0.5f + 1.0f, s_Near);
    const auto& colliders = collision.GetWorldColliders();
    const HeightFieldShape* ground = nullptr;   // 起伏の地面（下り坂の吸い付けを最後にやる）
    const bool round = col.shape == ColliderShape::Capsule || col.shape == ColliderShape::Sphere;
    const float footDrop = (col.shape == ColliderShape::Capsule) ? col.height * 0.5f : 0.0f;   // 中心 → 下の半球の中心
    for (int idx : s_Near)
    {
        const auto& wc = colliders[idx];
        if (wc.entity == self) continue;

        // 相手が静的地形か？（Rigidbody を持ち isStatic、または Rigidbody 無し=静的扱い）
        if (wc.hasRigidbody && !wc.fixed) continue;   // 動的同士は今は無視

        // 起伏の地面（2026-10-04）: 足元の球（下の半球の中心）が地面の接平面から半径だけ上に来るまで真上へ押し上げる
        // （法線の向きに押すと、立っているだけで坂を少しずつずり落ちる）。歩ける傾き（法線 y > 0.5）なら接地。
        // 地面は「中心の真下のマスの式」で測るので、崖（区域の境）では混ざらない。崖の壁は箱が止める
        if (wc.shape == ColliderShape::HeightField)
        {
            if (!round || !wc.heightField) continue;
            const Vector3 foot = selfCenter - Vector3(0.0f, footDrop, 0.0f);
            if (!wc.heightField->Contains(foot.x, foot.z)) continue;
            ground = wc.heightField.get();
            const Vector3 n = wc.heightField->Normal(foot.x, foot.z);
            const float dy = wc.heightField->Height(foot.x, foot.z) + col.radius / (std::max)(n.y, 0.2f) - foot.y;
            if (dy <= 0.0f) continue;
            tf.position.y += dy;
            selfCenter = tf.position + col.offset;
            if (n.y > 0.5f)
            {
                if (rb.velocity.y < 0.0f) rb.velocity.y = 0.0f;
                if (!grounded || n.y < groundNormal.y) groundNormal = n;
                grounded = true;
            }
            else if (rb.response == ResponseMode::Slide)
                rb.velocity = PhysicsMath::SlideVelocity(rb.velocity, n);
            continue;
        }

        // 自分 Capsule vs 相手 AABB の Contact を取る
        Contact contact;
        bool hit = false;

        if (col.shape == ColliderShape::Capsule && wc.shape == ColliderShape::AABB)
        {
            Capsule selfCap{ selfCenter, col.radius, col.height };
            AABB otherBox{ wc.center - wc.halfExtents, wc.center + wc.halfExtents };
            hit = IntersectCapsuleAABB(selfCap, otherBox, contact);
        }
        else if (col.shape == ColliderShape::Capsule && wc.shape == ColliderShape::Convex)
        {
            // 斜面: 法線が斜めに返るので、Slide 応答でそのまま登り降りになる。
            // 接地判定（normal.y > 0.5）は 60° までを床扱いにする
            Capsule selfCap{ selfCenter, col.radius, col.height };
            hit = IntersectCapsuleConvex(selfCap, wc.hull, contact);
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
            {
                if (!grounded || contact.normal.y < groundNormal.y)
                    groundNormal = contact.normal;
                grounded = true;
            }
        }
    }
    // 下り坂の吸い付け（2026-10-04）: 前のフレームに接地していて、上へ跳んでいず、今フレームは何にも乗っていない時、
    // 起伏の地面が kSnapDown 以内の下にあれば降ろして接地のままにする（走って丘を下ると毎フレーム少し宙に浮き、
    // 接地が点滅して落下のアニメ・跳べない・足音が乱れた）。台地の縁から落ちる（段差が大きい）時は吸い付けない
    constexpr float kSnapDown = 0.35f;
    if (!grounded && ground && rb.isGrounded && rb.velocity.y <= 0.5f)
    {
        const Vector3 foot = selfCenter - Vector3(0.0f, footDrop, 0.0f);
        const Vector3 n = ground->Normal(foot.x, foot.z);
        const float dy = ground->Height(foot.x, foot.z) + col.radius / (std::max)(n.y, 0.2f) - foot.y;
        if (n.y > 0.5f && dy < 0.0f && dy > -kSnapDown)
        {
            tf.position.y += dy;
            if (rb.velocity.y < 0.0f) rb.velocity.y = 0.0f;
            groundNormal = n;
            grounded = true;
        }
    }
    rb.groundNormal = grounded ? groundNormal : Vector3(0.0f, 1.0f, 0.0f);
    return grounded;
}

void PhysicsSystem::Update(Registry& reg, float dt, CollisionSystem& collision)
{
    // 動的 Rigidbody を移動 + 応答
    reg.CreateView<TransformComponent, ColliderComponent, RigidbodyComponent>()   // 剛体を持つ物だけ回す
        .EachFrom<RigidbodyComponent>([&](Entity e, TransformComponent& tf, ColliderComponent& col, RigidbodyComponent& rb)
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