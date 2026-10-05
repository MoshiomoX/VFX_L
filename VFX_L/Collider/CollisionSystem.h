// ============================================================
// CollisionSystem.h
// 衝突判定 System
// 毎フレーム: ワールド形状を収集 → レイヤーフィルタ + 形状別判定 → 衝突ペアを公開
// 対応形状: Sphere vs Sphere / Sphere vs Capsule（垂直カプセル）
// Capsule vs Capsule は未対応（敵同士の押し出しをやる時に追加）
//
// 固定（Rigidbody を持ち isStatic: 地形・置物の衝突箱・報酬の箱・エリートの的）と
// 可動（それ以外: プレイヤー・CPU の弾など）に分ける。
//   固定同士は判定しない（使う所が無い。AABB / Convex 同士はそもそも未対応）。
//   固定は格子に載せて使い回し、集合が変わるか、どれかが余白以上動いた時だけ作り直す。
//   可動は毎フレーム、自分のマスの固定 + 可動全部と判定する（可動は数個）。
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
        bool                  hasRigidbody;    // RigidbodyComponent を持つか
        bool                  fixed;           // Rigidbody を持ち isStatic
        CollisionMath::Convex hull;             // Convex 用（ワールド空間へ平行移動済み）
        std::shared_ptr<const CollisionMath::HeightFieldShape> heightField;   // HeightField 用（世界座標）
    };
    const std::vector<WorldCollider>& GetWorldColliders() const { return m_WorldColliders; }

    // 物理の押し出し用: center の水平 reach 以内に掛かり得る固定の collider と、
    // Rigidbody を持たない collider（物理は静的扱い）の m_WorldColliders 添字。
    // 全件を回さずに済ませる（重複無し）
    void GatherStaticNear(const DirectX::SimpleMath::Vector3& center, float reach, std::vector<int>& out) const;

    int GetFixedGridRebuilds() const { return m_FixedRebuilds; }   // 固定の格子を作り直した回数（調整用）
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
    RaycastResult Raycast(const CollisionMath::Ray& ray, uint32_t layerMask = 0xFFFFFFFF) const;
    // 範囲内の Entity を取得（AOE / 索敵用）
    std::vector<Entity> OverlapSphere(const DirectX::SimpleMath::Vector3& center,
        float radius, uint32_t layerMask) const;

    // 範囲内で最も近い Entity を1つ取得。見つかれば true。
    bool FindNearestEntity(const DirectX::SimpleMath::Vector3& center,
        float radius, uint32_t layerMask, Entity& outEntity) const;

private:
    // ナローフェーズ: 1対の判定（ブロードフェーズの格子から呼ばれる）
    void TestPair(const WorldCollider& a, const WorldCollider& b);

    // 固定の格子（CSR）を今の m_FixedIdx から作り直す
    void BuildFixedGrid();
    // 格子のマス範囲（格子の外へはみ出す分は切る。重ならなければ false）
    bool GridRange(float minX, float maxX, float minZ, float maxZ, int& x0, int& x1, int& z0, int& z1) const;
    // 固定の k 番目のうち、水平範囲に掛かり得る物を m_Scratch へ（重複無し）
    void GatherFixed(float minX, float maxX, float minZ, float maxZ) const;

    // 固定が前（0〜固定の数-1、並びが変わらない限り作り直さない）、可動が後ろ（毎フレーム）
    std::vector<WorldCollider> m_WorldColliders;
    std::vector<CollisionPair> m_Pairs;

    std::vector<int> m_FixedIdx;     // 固定の k 番目 → m_WorldColliders の添字（= k）
    std::vector<int> m_MoverIdx;     // 可動（毎フレーム）
    std::vector<std::pair<Entity, DirectX::SimpleMath::Vector3>> m_ScanFixed;   // 毎フレームの走査：固定の (entity, 中心)
    std::vector<WorldCollider> m_MoverWorld;                                     // 同：可動
    std::vector<Entity> m_GridEntity;                       // 格子を作った時の固定の並び（変化の検出）
    std::vector<DirectX::SimpleMath::Vector3> m_GridCenter; // 同、中心
    int m_GridX0 = 0, m_GridZ0 = 0, m_GridW = 0, m_GridD = 0;   // マス単位
    std::vector<int> m_CellStart;    // マス毎の m_CellItems の始まり（W*D+1）
    std::vector<int> m_CellItems;    // 固定の k
    std::vector<int> m_LargeFixed;   // 格子に載せない大きい固定（床・外周の崖）: k
    mutable std::vector<int> m_Scratch;
    int m_FixedRebuilds = 0;
};
