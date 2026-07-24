// ============================================================
// CollisionSystem.cpp
// ============================================================
#include "CollisionSystem.h"
#include "TransformComponent.h"
#include "ColliderComponent.h"
#include "View.h"

using DirectX::SimpleMath::Vector3;

// ============================================================
// 2つの WorldCollider が交差するか（形状組み合わせで数学を振り分け）
// ============================================================
static bool TestPair(const CollisionSystem::WorldCollider& a,
    const CollisionSystem::WorldCollider& b)
{
    using namespace CollisionMath;

    // AABB を min/max 形式に変換するヘルパ
    auto toAABB = [](const CollisionSystem::WorldCollider& w) -> AABB
        {
            return { w.center - w.halfExtents, w.center + w.halfExtents };
        };

    // --- Sphere vs Sphere ---
    if (a.shape == ColliderShape::Sphere && b.shape == ColliderShape::Sphere)
        return IntersectSphereSphere({ a.center, a.radius }, { b.center, b.radius });

    // --- Sphere vs Capsule ---
    if (a.shape == ColliderShape::Sphere && b.shape == ColliderShape::Capsule)
        return IntersectSphereCapsule({ a.center, a.radius }, { b.center, b.radius, b.height });
    if (a.shape == ColliderShape::Capsule && b.shape == ColliderShape::Sphere)
        return IntersectSphereCapsule({ b.center, b.radius }, { a.center, a.radius, a.height });

    // --- Sphere vs AABB ---
    if (a.shape == ColliderShape::Sphere && b.shape == ColliderShape::AABB)
        return IntersectSphereAABB({ a.center, a.radius }, toAABB(b));
    if (a.shape == ColliderShape::AABB && b.shape == ColliderShape::Sphere)
        return IntersectSphereAABB({ b.center, b.radius }, toAABB(a));

    // --- Capsule vs AABB ---
    if (a.shape == ColliderShape::Capsule && b.shape == ColliderShape::AABB)
        return IntersectCapsuleAABB({ a.center, a.radius, a.height }, toAABB(b));
    if (a.shape == ColliderShape::AABB && b.shape == ColliderShape::Capsule)
        return IntersectCapsuleAABB({ b.center, b.radius, b.height }, toAABB(a));

    // --- AABB vs AABB は未対応（地形は静的、互いに判定しない）---
    return false;
}
// ============================================================
// Update
// ============================================================
void CollisionSystem::Update(Registry& reg)
{
    m_WorldColliders.clear();
    m_Pairs.clear();

    // --- 1) ローカル形状 → ワールド形状へ変換して収集 ---
    //     ※垂直カプセル/球は回転不変なので rotation は考慮しない
    reg.CreateView<TransformComponent, ColliderComponent>()
        .Each([this](Entity e, TransformComponent& tf, ColliderComponent& col)
            {
                WorldCollider wc;
                wc.entity = e;
                wc.shape = col.shape;
                wc.center = tf.position + col.offset;
                wc.radius = col.radius;
                wc.height = col.height;
                wc.halfExtents = col.halfExtents;
                wc.layer = col.layer;
                wc.mask = col.mask;
                m_WorldColliders.push_back(wc);
            });

    // --- 2) 総当たり判定（レイヤーフィルタ + 形状分派）---
    //  空間分割を導入する場合はこの二重ループを差し替える（挿入ポイント）
    const size_t n = m_WorldColliders.size();
    for (size_t i = 0; i < n; ++i)
    {
        for (size_t j = i + 1; j < n; ++j)
        {
            const WorldCollider& A = m_WorldColliders[i];
            const WorldCollider& B = m_WorldColliders[j];

            // レイヤーフィルタ: 双方が相手の層を許可している時だけ判定
            if (!((A.layer & B.mask) && (B.layer & A.mask)))
                continue;

            if (TestPair(A, B))
                m_Pairs.push_back({ A.entity, B.entity });
        }
    }
}
// ============================================================
// シーン全体レイキャスト（最近命中を返す）
// ============================================================
CollisionSystem::RaycastResult CollisionSystem::Raycast(
    const CollisionMath::Ray& ray, uint32_t layerMask)
{
    using namespace CollisionMath;
    RaycastResult best;
    best.t = ray.maxDist;   // これより近い命中だけ採用

    for (const auto& wc : m_WorldColliders)
    {
        // レイヤーフィルタ
        if (!(wc.layer & layerMask)) continue;

        RayHit h;
        switch (wc.shape)
        {
        case ColliderShape::Sphere:
            h = RaycastSphere(ray, { wc.center, wc.radius });
            break;
        case ColliderShape::Capsule:
            h = RaycastCapsule(ray, { wc.center, wc.radius, wc.height });
            break;
        case ColliderShape::AABB:
            h = RaycastAABB(ray, { wc.center - wc.halfExtents, wc.center + wc.halfExtents });
            break;
        default:
            continue;
        }

        if (h.hit && h.t < best.t)
        {
            best.hit = true;
            best.entity = wc.entity;
            best.t = h.t;
            best.point = h.point;
            best.normal = h.normal;
        }
    }
    return best;
}
// ============================================================
// 範囲クエリ（gameplay からの能動的な問い合わせ）
// ============================================================
std::vector<Entity> CollisionSystem::OverlapSphere(
    const Vector3& center, float radius, uint32_t layerMask) const
{
    using namespace CollisionMath;
    Sphere query{ center, radius };
    std::vector<Entity> result;

    for (const auto& wc : m_WorldColliders)
    {
        if (!(wc.layer & layerMask)) continue;

        bool hit = false;
        switch (wc.shape)
        {
        case ColliderShape::Sphere:
            hit = IntersectSphereSphere(query, { wc.center, wc.radius });
            break;
        case ColliderShape::Capsule:
            hit = IntersectSphereCapsule(query, { wc.center, wc.radius, wc.height });
            break;
        case ColliderShape::AABB:
            hit = IntersectSphereAABB(query,
                { wc.center - wc.halfExtents, wc.center + wc.halfExtents });
            break;
        }
        if (hit) result.push_back(wc.entity);
    }
    return result;
}

bool CollisionSystem::FindNearestEntity(
    const Vector3& center, float radius, uint32_t layerMask, Entity& outEntity) const
{
    float bestDistSq = radius * radius;
    bool found = false;

    for (const auto& wc : m_WorldColliders)
    {
        if (!(wc.layer & layerMask)) continue;

        float distSq = (wc.center - center).LengthSquared();
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            outEntity = wc.entity;
            found = true;
        }
    }
    return found;
}