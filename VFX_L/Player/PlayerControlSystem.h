// ============================================================
// PlayerControlSystem.h
// プレイヤー操作 System
// 入力 → Rigidbody の水平速度 に変換する。
// ※垂直（velocity.y）には触らない。重力と着地は PhysicsSystem の担当。
// ============================================================
#pragma once

class Registry;
class CameraBase;

class PlayerControlSystem
{
public:
    void Update(Registry& reg, float dt, CameraBase* camera);
    void SetMoveSpeed(float s) { m_MoveSpeed = s; }
    void SetJumpPower(float p) { m_JumpPower = p; }
private:
    float m_JumpPower = 8.0f;
    float m_MoveSpeed = 5.0f;   // 水平移動速度
};