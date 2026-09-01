// ============================================================
// TestSpawner.h
// テスト用 Entity を1行で生成するファクトリ関数群。
// 物理/衝突テストのシーン構築を高速化するための道具。
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include <SimpleMath.h>

class Registry;

namespace TestSpawner
{
    using DirectX::SimpleMath::Vector3;

    // 静的な地形ブロック（AABB、動かない、重力なし）
    Entity SpawnStaticBox(Registry& reg, const Vector3& pos, const Vector3& halfExtents);

    // 落下する球（Sphere、重力あり、Slide 応答）
    Entity SpawnDynamicSphere(Registry& reg, const Vector3& pos, float radius);

    // キャラクター用カプセル（Capsule、重力あり、Slide 応答）
    Entity SpawnCapsule(Registry& reg, const Vector3& pos, float radius, float height);

    // 静的なカプセル/球（動かない障害物用、重力なし）
    Entity SpawnStaticSphere(Registry& reg, const Vector3& pos, float radius);
}