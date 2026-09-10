// ============================================================
// CollisionSystem.cpp
//
// 広相位は uniform grid。
//   全対全の O(n^2) を「同じマスの中だけ総当たり」に落とす。
//   格子は毎フレーム作り直す（増分更新は書かない。どうせ
//   m_WorldColliders を毎フレーム収集し直すので、その流れで
//   登記するのが一番単純で一番壊れない）。
//
// 完備性の理屈:
//   各 collider は自分の AABB が覆う全マスに登記される。
//   交差している2つの collider は必ず少なくとも1マスを共有する。
//   よって同マス内の総当たりだけで漏れは無い（隣接マス探索は不要）。
//   代わりに、大きい collider は複数マスで同じ対が見つかるので
//   pair の重複を set で弾く。
// ============================================================
#include "Collider/CollisionSystem.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "ECS/View.h"
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>

using DirectX::SimpleMath::Vector3;

namespace
{
    // 広相位のマスの一辺。
    // 大半の動的 collider（半径 0.25~0.5）より十分大きく、
    // 巨大な静的 AABB（地形・床）は複数マスへまたがって登記される
    constexpr float kBroadCell = 4.0f;

    // セル座標 → ハッシュキー（場地が原点中心なので負もあり得る）
    inline int64_t CellKey(int cx, int cz)
    {
        return ((int64_t)cx << 32) ^ (uint32_t)cz;
    }
}

// ============================================================
// 2つの WorldCollider が交差するか（形状組み合わせで数学を振り分け）
// ※旧版から無変更
// ============================================================
static bool TestPairShape(const CollisionSystem::WorldCollider& a,
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

    // --- Capsule vs Capsule ---
    // 未対応（敵同士の押し出しをやる時に追加）

    // --- AABB vs AABB は未対応（地形は静的、互いに判定しない）---
    return false;
}

// ============================================================
// 狭相位: 1対の判定（広相位の格子から呼ばれる）
// レイヤーフィルタ → 形状判定 → 命中なら m_Pairs へ。
// 中身のロジックは旧の二重ループの内側と同一
// ============================================================
void CollisionSystem::TestPair(const WorldCollider& a, const WorldCollider& b)
{
    // レイヤーフィルタ: 双方が相手の層を許可している時だけ判定
    if (!((a.layer & b.mask) && (b.layer & a.mask)))
        return;

    if (TestPairShape(a, b))
        m_Pairs.push_back({ a.entity, b.entity });
}

// ============================================================
// Update
// 1) ワールド形状の収集  2) 格子へ登記  3) 同マス内だけ判定
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

    // --- 2) 各 collider を、その AABB が覆う全マスへ登記 ---
    // static でメモリを使い回す（毎フレームの再確保を避ける）
    static std::unordered_map<int64_t, std::vector<int>> cells;
    for (auto& kv : cells) kv.second.clear();

    auto boundsOf = [](const WorldCollider& wc,
        float& minX, float& maxX, float& minZ, float& maxZ)
        {
            if (wc.shape == ColliderShape::AABB)
            {
                minX = wc.center.x - wc.halfExtents.x;
                maxX = wc.center.x + wc.halfExtents.x;
                minZ = wc.center.z - wc.halfExtents.z;
                maxZ = wc.center.z + wc.halfExtents.z;
            }
            else   // Sphere / Capsule は水平方向は半径で足りる
            {
                minX = wc.center.x - wc.radius;
                maxX = wc.center.x + wc.radius;
                minZ = wc.center.z - wc.radius;
                maxZ = wc.center.z + wc.radius;
            }
        };

    for (int i = 0; i < (int)m_WorldColliders.size(); ++i)
    {
        float minX, maxX, minZ, maxZ;
        boundsOf(m_WorldColliders[i], minX, maxX, minZ, maxZ);

        const int cx0 = (int)std::floor(minX / kBroadCell);
        const int cx1 = (int)std::floor(maxX / kBroadCell);
        const int cz0 = (int)std::floor(minZ / kBroadCell);
        const int cz1 = (int)std::floor(maxZ / kBroadCell);

        for (int cz = cz0; cz <= cz1; ++cz)
            for (int cx = cx0; cx <= cx1; ++cx)
                cells[CellKey(cx, cz)].push_back(i);
    }

    // --- 3) 同じマスの中だけ総当たり ---
    // 大きい collider は複数マスに登記されているので、
    // 同じ対が複数マスで見つかる。重複は set で弾く
    static std::unordered_set<uint64_t> seen;
    seen.clear();

    for (const auto& kv : cells)
    {
        const auto& list = kv.second;
        const size_t n = list.size();

        for (size_t a = 0; a + 1 < n; ++a)
        {
            for (size_t b = a + 1; b < n; ++b)
            {
                int i = list[a], j = list[b];
                if (i > j) std::swap(i, j);

                const uint64_t key = ((uint64_t)i << 32) | (uint32_t)j;
                if (!seen.insert(key).second) continue;   // 別マスで判定済み

                TestPair(m_WorldColliders[i], m_WorldColliders[j]);
            }
        }
    }
}

// ============================================================
// シーン全体レイキャスト（最近命中を返す）
// ※以下の3つのクエリは m_WorldColliders の線形走査のまま。
//   毎フレーム数回しか呼ばれないので格子に載せる必要が無い
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