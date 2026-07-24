// ============================================================
// ProjectileComponent.h
// 投射物データ（純データ）
// 形状は ColliderComponent(Sphere) 側が持つ。
// ============================================================
#pragma once
#include <SimpleMath.h>

struct ProjectileComponent
{
    DirectX::SimpleMath::Vector3 velocity = { 0, 0, 0 };  // 飛行速度
    float damage = 10.0f;
    float lifetime = 3.0f;   // 最大生存時間（秒）
    float age = 0.0f;   // 経過時間
};