// ============================================================
// ColliderComponent.h
// 衝突形状（純データ、ローカル空間）
// 第1版形状: Sphere（球） / Capsule（垂直カプセル）
// 垂直カプセル前提 → 回転は考慮しない（端点は中心±高さで算出）
// ============================================================
#pragma once
#include <SimpleMath.h>

// 形状タイプ
enum class ColliderShape
{
    Sphere,
    Capsule,   // 垂直カプセル（Y軸方向に立つ）
    AABB,
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

    // --- レイヤー ---
    uint32_t layer = Layer_Enemy;      // 自分が属する層
    uint32_t mask = Layer_All;        // 衝突を許可する相手の層（この層だけ判定する）
};