// ============================================================
// ColliderComponent.h
// 衝突形状（純データ、ローカル空間）
// 第1版形状: Sphere（球） / Capsule（垂直カプセル）
// 垂直カプセル前提 → 回転は考慮しない（端点は中心±高さで算出）
// ============================================================
#pragma once
#include <memory>
#include <SimpleMath.h>
#include "Collider/CollisionMath.h"   // Convex / HeightFieldShape

// 形状タイプ
enum class ColliderShape
{
    Sphere,
    Capsule,   // 垂直カプセル（Y軸方向に立つ）
    AABB,
    Convex,    // 凸多面体（平面集合）。台形柱・斜面など、軸に揃わない静的地形
    HeightField,   // 起伏のある地面（2026-10-04）。高さは heightField に問い合わせる。halfExtents = ブロードフェーズ用の範囲
};

// 衝突レイヤー（ビットフラグ。どの層と衝突するかを mask で制御）
enum CollisionLayer : uint32_t
{
    Layer_None = 0,
    Layer_Player = 1 << 0,
    Layer_Enemy = 1 << 1,
    Layer_PlayerShot = 1 << 2,   // プレイヤーの投射物
    Layer_EnemyShot = 1 << 3,   // 敵の投射物
    Layer_Terrain = 1 << 4,
    Layer_Prop = 1 << 5,   // 木・岩など地形の上の置物（ぶつかるが、カメラの遮蔽判定は見ない）
    Layer_All = 0xFFFFFFFF,
};

struct ColliderComponent
{
    ColliderShape shape = ColliderShape::Sphere;

    // --- 位置調整 ---
    DirectX::SimpleMath::Vector3 offset = { 0, 0, 0 };  // Entity 位置からのローカルオフセット
    DirectX::SimpleMath::Vector3 halfExtents = { 0.5f, 0.5f, 0.5f };  // 各軸の半サイズ

    // --- 形状パラメータ ---
    float radius = 0.5f;   // Sphere/Capsule 共通: 半径
    float height = 1.0f;   // Capsule 専用: 円柱部分の高さ（両端の半球は含まない）

    // Convex 専用: Entity 位置（+offset）を原点とするローカル平面。
    // 回転は見ない（他の形状と同じ約束）。halfExtents はブロードフェーズ用の包囲箱として必ず埋める。
    // 組み立ては CollisionMath::ConvexFromHexahedron 等で
    CollisionMath::Convex hull;
    // Convex 専用: 水平にしか押し返さない（壁扱い。上面に乗れない・登れない）。外周の巨石（2026-10-06、ユーザー：
    // 石の上に立つと凸包と形の差で浮いて見える）。物理が法線を水平へ倒し、接地にしない
    bool wallOnly = false;

    // HeightField 専用: 地面の高さの問い合わせ先（世界座標。Entity の位置は使わない）
    std::shared_ptr<const CollisionMath::HeightFieldShape> heightField;

    // --- レイヤー ---
    uint32_t layer = Layer_Enemy;      // 自分が属する層
    uint32_t mask = Layer_All;        // 衝突を許可する相手の層（この層だけ判定する）
};