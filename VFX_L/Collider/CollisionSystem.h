// ============================================================
// CollisionSystem.h
// 衝突判定 System
// 毎フレーム: ワールド形状を収集 → レイヤーフィルタ + 形状別判定 → 衝突ペアを公開
// 対応形状: Sphere vs Sphere / Sphere vs Capsule（垂直カプセル）
// Capsule vs Capsule は未対応（敵同士の押し出しをやる時に追加）
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "Collider/CollisionMath.h"
#include "Component/ColliderComponent.h"   // ColliderShape / CollisionLayer
#include <vector>
#include <cstdint>

class Registry;

// 衝突した Entity のペア
struct CollisionPair
{
    Entity a;
    Entity b;
};

class CollisionSystem
{
public:
    void Update(Registry& reg);

    // 今フレームの衝突ペア（gameplay 側が参照）
    const std::vector<CollisionPair>& GetPairs() const { return m_Pairs; }

    // ワールド空間の衝突体（形状情報付き）
    struct WorldCollider
    {
        Entity                entity;
        ColliderShape         shape;
        DirectX::SimpleMath::Vector3 center;   // ワールド中心（tf.position + offset）
        float                 radius;
        float                 height;          // Capsule 用
        DirectX::SimpleMath::Vector3 halfExtents;
        uint32_t              layer;
        uint32_t              mask;
    };
    const std::vector<WorldCollider>& GetWorldColliders() const { return m_WorldColliders; }
    // レイキャスト結果（命中 Entity 付き）
    struct RaycastResult
    {
        bool    hit = false;
        Entity  entity = 0;
        float   t = 0.0f;
        DirectX::SimpleMath::Vector3 point = {};
        DirectX::SimpleMath::Vector3 normal = {};
    };

    // シーン全体へレイキャスト。layerMask に含まれる層のみ対象。最も近い命中を返す。
    RaycastResult Raycast(const CollisionMath::Ray& ray, uint32_t layerMask = 0xFFFFFFFF);
    // 範囲内の Entity を取得（AOE / 索敵用）
    std::vector<Entity> OverlapSphere(const DirectX::SimpleMath::Vector3& center,
        float radius, uint32_t layerMask) const;

    // 範囲内で最も近い Entity を1つ取得。見つかれば true。
    bool FindNearestEntity(const DirectX::SimpleMath::Vector3& center,
        float radius, uint32_t layerMask, Entity& outEntity) const;

private:
    std::vector<WorldCollider> m_WorldColliders;  // 毎フレーム再構築
    std::vector<CollisionPair> m_Pairs;
};