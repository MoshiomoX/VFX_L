// ============================================================
// PhysicsSystem.h
// 浅い物理: 重力 + 移動 + 衝突応答（push-out / slide / bounce）
// 動的な Rigidbody を動かし、静的な地形と衝突させて応答する。
// ============================================================
#pragma once

class Registry;
class CollisionSystem;


class PhysicsSystem
{
public:
    void Update(Registry& reg, float dt, CollisionSystem& collision);
    void SetGravity(float g) { m_Gravity = g; }
private:
    float m_Gravity = -20.0f;   // 重力加速度（m/s^2、負=下向き）
};