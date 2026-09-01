// ============================================================
// TransformComponent.h
// ECS 用 Transform（純データ）
// ECS 世界における位置・回転・拡縮の唯一の権威。
// GameObject::Transform とは無関係（あちらは廃止予定）。
// ============================================================
#pragma once
#include <SimpleMath.h>

struct TransformComponent
{
    DirectX::SimpleMath::Vector3 position = { 0, 0, 0 };
    DirectX::SimpleMath::Vector3 rotation = { 0, 0, 0 };  // Euler角（度数、既存Transformと同じ規約）
    DirectX::SimpleMath::Vector3 scale = { 1, 1, 1 };  // 衝突第1版では未使用（描画移行用に確保）
};