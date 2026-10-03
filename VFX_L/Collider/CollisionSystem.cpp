// ============================================================
// CollisionSystem.cpp
//
// 広相位は uniform grid。ただし格子に載せるのは固定（Rigidbody を持ち isStatic）の collider だけで、
// 毎フレームは作り直さない（2026-09-28: 毎フレーム全部を unordered_map の格子へ登記し直していた頃は
// Debug で 2.5 ms。200m の床の箱 1 つで 2,500 マス、さらに固定同士の対を set で重複除去していた）。
//   作り直すのは、固定の並び（Entity）が変わった時か、どれかの中心が kFixedSlack 以上動いた時。
//   各固定は AABB + kFixedSlack が覆うマスに登記するので、報酬の箱の浮き沈み程度では作り直さない。
//   覆うマスが kLargeCells を超える物（床・外周の崖・大きい高台）は格子に載せず、毎回全員と比べる。
//
// 対の作り方:
//   可動 × 固定: 可動の AABB が覆うマスの固定 + 大きい固定。重複はソートで除く
//   可動 × 可動: 総当たり（可動は玩家・CPU の弾など数個）
//   固定 × 固定: 判定しない
// 完備性: 交差している可動と固定は、少なくとも 1 マスを共有するか、固定が大きい方の一覧にいる。
// ============================================================
#include "Collider/CollisionSystem.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "ECS/View.h"
#include <cmath>
#include <algorithm>

using DirectX::SimpleMath::Vector3;

namespace
{
    // 広相位のマスの一辺。
    // 大半の動的 collider（半径 0.25~0.5）より十分大きく、
    // 巨大な静的 AABB（地形・床）は複数マスへまたがって登記される
    constexpr float kBroadCell = 4.0f;
    // 固定がこれ以上動いたら格子を作り直す（登記もこの分だけ広げる）
    constexpr float kFixedSlack = 0.5f;
    // これより多くのマスを覆う固定は格子に載せない
    constexpr int kLargeCells = 64;

    // collider の水平の範囲
    void BoundsOf(const CollisionSystem::WorldCollider& wc, float& minX, float& maxX, float& minZ, float& maxZ)
    {
        if (wc.shape == ColliderShape::AABB || wc.shape == ColliderShape::Convex)
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
    }
    int CellOf(float v) { return (int)std::floor(v / kBroadCell); }
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

    // --- Sphere / Capsule vs Convex ---
    if (a.shape == ColliderShape::Sphere && b.shape == ColliderShape::Convex)
        return IntersectSphereConvex({ a.center, a.radius }, b.hull);
    if (a.shape == ColliderShape::Convex && b.shape == ColliderShape::Sphere)
        return IntersectSphereConvex({ b.center, b.radius }, a.hull);
    if (a.shape == ColliderShape::Capsule && b.shape == ColliderShape::Convex)
        return IntersectCapsuleConvex({ a.center, a.radius, a.height }, b.hull);
    if (a.shape == ColliderShape::Convex && b.shape == ColliderShape::Capsule)
        return IntersectCapsuleConvex({ b.center, b.radius, b.height }, a.hull);

    // --- Capsule vs Capsule ---
    // 未対応（敵同士の押し出しをやる時に追加）

    // --- AABB vs AABB / Convex 同士は未対応（地形は静的、互いに判定しない）---
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
// 1) ワールド形状の収集（固定 / 可動に分ける）  2) 固定の格子が古ければ作り直す
// 3) 可動 × 固定（格子）と 可動 × 可動 だけ判定
// ============================================================
// ローカル形状 → ワールド形状
// ※垂直カプセル/球は回転不変なので rotation は考慮しない
static CollisionSystem::WorldCollider MakeWorldCollider(Entity e, const TransformComponent& tf,
    const ColliderComponent& col, bool hasRigidbody, bool fixed)
{
    CollisionSystem::WorldCollider wc;
    wc.entity = e;
    wc.shape = col.shape;
    wc.center = tf.position + col.offset;
    wc.radius = col.radius;
    wc.height = col.height;
    wc.halfExtents = col.halfExtents;
    wc.layer = col.layer;
    wc.mask = col.mask;
    wc.hasRigidbody = hasRigidbody;
    wc.fixed = fixed;
    if (col.shape == ColliderShape::Convex)
    {
        // ローカル平面 n·p <= d を中心分だけ平行移動: d' = d + n·center
        wc.hull = col.hull;
        for (int i = 0; i < wc.hull.count; ++i)
            wc.hull.planes[i].d += wc.hull.planes[i].n.Dot(wc.center);
    }
    else
        wc.hull.count = 0;
    return wc;
}

void CollisionSystem::Update(Registry& reg)
{
    m_Pairs.clear();
    m_MoverIdx.clear();

    // --- 1) 走査。固定は (entity, 中心) だけ集め、可動は毎フレーム作る ---
    // 固定の WorldCollider（凸体は平面 12 枚を持つ）を毎フレーム全部作り直していた。
    // 外周の岩に衝突を 131 個足した時に Debug で約 0.15 ms 増えたので、固定は前回の物を使い回す（2026-10-03）
    m_ScanFixed.clear();
    m_MoverWorld.clear();
    reg.CreateView<TransformComponent, ColliderComponent>()   // 衝突を持つ物だけ回す（装飾物の Transform は見ない）
        .EachFrom<ColliderComponent>([this, &reg](Entity e, TransformComponent& tf, ColliderComponent& col)
            {
                const bool hasRb = reg.Has<RigidbodyComponent>(e);
                const bool fixed = hasRb && reg.Get<RigidbodyComponent>(e).isStatic;
                if (fixed) m_ScanFixed.push_back({ e, tf.position + col.offset });
                else       m_MoverWorld.push_back(MakeWorldCollider(e, tf, col, hasRb, false));
            });

    // --- 2) 固定：並びが変わったら全部作り直して格子も。位置だけ変わった物はその 1 個を作り直し、
    //        余白以上動いていたら格子を作り直す ---
    const size_t nFixed = m_ScanFixed.size();
    bool structural = (nFixed != m_GridEntity.size()) || (m_WorldColliders.size() < nFixed);
    for (size_t k = 0; k < nFixed && !structural; ++k)
        structural = (m_ScanFixed[k].first != m_GridEntity[k]);
    bool rebuildGrid = structural;
    m_WorldColliders.resize(nFixed);
    if (structural)
    {
        m_FixedIdx.resize(nFixed);
        for (size_t k = 0; k < nFixed; ++k)
        {
            const Entity e = m_ScanFixed[k].first;
            m_WorldColliders[k] = MakeWorldCollider(e, reg.Get<TransformComponent>(e), reg.Get<ColliderComponent>(e), true, true);
            m_FixedIdx[k] = (int)k;
        }
    }
    else
    {
        for (size_t k = 0; k < nFixed; ++k)
        {
            if (m_ScanFixed[k].second == m_WorldColliders[k].center) continue;
            const Entity e = m_ScanFixed[k].first;
            m_WorldColliders[k] = MakeWorldCollider(e, reg.Get<TransformComponent>(e), reg.Get<ColliderComponent>(e), true, true);
            if ((m_WorldColliders[k].center - m_GridCenter[k]).LengthSquared() > kFixedSlack * kFixedSlack)
                rebuildGrid = true;
        }
    }
    if (rebuildGrid) BuildFixedGrid();

    // 可動は固定の後ろへ
    for (const WorldCollider& wc : m_MoverWorld)
    {
        m_MoverIdx.push_back((int)m_WorldColliders.size());
        m_WorldColliders.push_back(wc);
    }

    // --- 3) 可動 × 固定、可動 × 可動 ---
    for (size_t m = 0; m < m_MoverIdx.size(); ++m)
    {
        const WorldCollider& a = m_WorldColliders[m_MoverIdx[m]];
        float minX, maxX, minZ, maxZ;
        BoundsOf(a, minX, maxX, minZ, maxZ);
        GatherFixed(minX, maxX, minZ, maxZ);
        for (int k : m_Scratch)
            TestPair(a, m_WorldColliders[m_FixedIdx[k]]);

        for (size_t n = m + 1; n < m_MoverIdx.size(); ++n)
            TestPair(a, m_WorldColliders[m_MoverIdx[n]]);
    }
}

// ============================================================
// 固定の格子（CSR: マス毎の始まり + 固定の k の並び）
// ============================================================
void CollisionSystem::BuildFixedGrid()
{
    ++m_FixedRebuilds;
    const size_t n = m_FixedIdx.size();
    m_GridEntity.resize(n);
    m_GridCenter.resize(n);
    m_LargeFixed.clear();

    // マスの範囲（余白込み）。大きい物は格子に載せない
    struct Span { int x0, x1, z0, z1; };
    std::vector<Span> spans(n);
    std::vector<uint8_t> inGrid(n, 0);
    int gx0 = 0, gx1 = -1, gz0 = 0, gz1 = -1;
    for (size_t k = 0; k < n; ++k)
    {
        const WorldCollider& wc = m_WorldColliders[m_FixedIdx[k]];
        m_GridEntity[k] = wc.entity;
        m_GridCenter[k] = wc.center;
        float minX, maxX, minZ, maxZ;
        BoundsOf(wc, minX, maxX, minZ, maxZ);
        Span s = { CellOf(minX - kFixedSlack), CellOf(maxX + kFixedSlack),
                   CellOf(minZ - kFixedSlack), CellOf(maxZ + kFixedSlack) };
        spans[k] = s;
        if ((s.x1 - s.x0 + 1) * (s.z1 - s.z0 + 1) > kLargeCells)
        {
            m_LargeFixed.push_back((int)k);
            continue;
        }
        inGrid[k] = 1;
        if (gx1 < gx0) { gx0 = s.x0; gx1 = s.x1; gz0 = s.z0; gz1 = s.z1; }
        gx0 = (std::min)(gx0, s.x0); gx1 = (std::max)(gx1, s.x1);
        gz0 = (std::min)(gz0, s.z0); gz1 = (std::max)(gz1, s.z1);
    }

    m_GridX0 = gx0;
    m_GridZ0 = gz0;
    m_GridW = (gx1 >= gx0) ? gx1 - gx0 + 1 : 0;
    m_GridD = (gz1 >= gz0) ? gz1 - gz0 + 1 : 0;
    const size_t cells = (size_t)m_GridW * m_GridD;
    m_CellStart.assign(cells + 1, 0);
    for (size_t k = 0; k < n; ++k)
    {
        if (!inGrid[k]) continue;
        for (int z = spans[k].z0; z <= spans[k].z1; ++z)
            for (int x = spans[k].x0; x <= spans[k].x1; ++x)
                ++m_CellStart[(size_t)(z - gz0) * m_GridW + (x - gx0) + 1];
    }
    for (size_t c = 0; c < cells; ++c) m_CellStart[c + 1] += m_CellStart[c];
    m_CellItems.assign(m_CellStart[cells], 0);
    std::vector<int> fill(m_CellStart.begin(), m_CellStart.end() - 1);
    for (size_t k = 0; k < n; ++k)
    {
        if (!inGrid[k]) continue;
        for (int z = spans[k].z0; z <= spans[k].z1; ++z)
            for (int x = spans[k].x0; x <= spans[k].x1; ++x)
                m_CellItems[fill[(size_t)(z - gz0) * m_GridW + (x - gx0)]++] = (int)k;
    }
}

bool CollisionSystem::GridRange(float minX, float maxX, float minZ, float maxZ,
    int& x0, int& x1, int& z0, int& z1) const
{
    x0 = (std::max)(CellOf(minX) - m_GridX0, 0);
    x1 = (std::min)(CellOf(maxX) - m_GridX0, m_GridW - 1);
    z0 = (std::max)(CellOf(minZ) - m_GridZ0, 0);
    z1 = (std::min)(CellOf(maxZ) - m_GridZ0, m_GridD - 1);
    return x0 <= x1 && z0 <= z1;
}

void CollisionSystem::GatherFixed(float minX, float maxX, float minZ, float maxZ) const
{
    m_Scratch.clear();
    int x0, x1, z0, z1;
    if (GridRange(minX, maxX, minZ, maxZ, x0, x1, z0, z1))
    {
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
            {
                const size_t c = (size_t)z * m_GridW + x;
                for (int i = m_CellStart[c]; i < m_CellStart[c + 1]; ++i)
                    m_Scratch.push_back(m_CellItems[i]);
            }
    }
    m_Scratch.insert(m_Scratch.end(), m_LargeFixed.begin(), m_LargeFixed.end());
    std::sort(m_Scratch.begin(), m_Scratch.end());
    m_Scratch.erase(std::unique(m_Scratch.begin(), m_Scratch.end()), m_Scratch.end());
}

void CollisionSystem::GatherStaticNear(const Vector3& center, float reach, std::vector<int>& out) const
{
    out.clear();
    GatherFixed(center.x - reach, center.x + reach, center.z - reach, center.z + reach);
    for (int k : m_Scratch) out.push_back(m_FixedIdx[k]);
    // Rigidbody を持たない collider は物理では静的扱い（今は CPU の弾だけ。形が球なので押し出しには効かない）
    for (int i : m_MoverIdx)
        if (!m_WorldColliders[i].hasRigidbody) out.push_back(i);
}

// ============================================================
// シーン全体レイキャスト（最近命中を返す）
// ※以下の3つのクエリは m_WorldColliders の線形走査のまま。
//   毎フレーム数回しか呼ばれないので格子に載せる必要が無い
// ============================================================
CollisionSystem::RaycastResult CollisionSystem::Raycast(
    const CollisionMath::Ray& ray, uint32_t layerMask) const
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
        case ColliderShape::Convex:
            h = RaycastConvex(ray, wc.hull);
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
        case ColliderShape::Convex:
            hit = IntersectSphereConvex(query, wc.hull);
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
