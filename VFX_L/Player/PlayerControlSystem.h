// ============================================================
// PlayerControlSystem.h
// プレイヤー操作 System
// 入力 → Rigidbody の水平速度 に変換する。
// ※垂直（velocity.y）には触らない（跳ぶ瞬間を除く）。重力と着地は PhysicsSystem の担当。
//
// 普段は入力方向 * moveSpeed をそのまま入れる（即応）。
// 滑り（左 Ctrl / パッド X）の間は水平速度を持ち越して、坂の下りで加速・摩擦で減速する。
// 滑りで moveSpeed を超えた勢いは、やめた後・跳んだ後も少しずつ減りながら残る。
// 数値は PlayerStatsComponent の slide* / momentum*
// ============================================================
#pragma once
#include <SimpleMath.h>

class Registry;
class CameraBase;

class PlayerControlSystem
{
public:
    void Update(Registry& reg, float dt, CameraBase* camera);

    // 被弾のノックバック（2026-10-01）。dir = 打たれた向き（敵 / 爆心 → 玩家、XZ。長さは問わない）、
    // blast = 爆発（遠くへ、少し浮く）。距離・時間は PlayerStatsComponent の knock*。
    // 入れた後の knockDuration 秒は水平速度に上乗せし、入力の効きは 0 から戻す
    static void ApplyKnockback(Registry& reg, unsigned int entity, DirectX::SimpleMath::Vector2 dir, bool blast);

    // TEMP-TEST: 自測（VFXL_BATTLE_AUTOTEST=slide）が入力の代わりに入れる
    bool testInput = false;
    bool testSlide = false;
    bool testJump = false;   // 1 フレームだけ立てる
    bool testSurge = false;  // 魔力解放（1 フレームだけ立てる。読んだら下ろす）
    DirectX::SimpleMath::Vector2 testMove = { 0.0f, 0.0f };
    void SetMoveSpeed(float s) { m_MoveSpeed = s; }
    void SetJumpPower(float p) { m_JumpPower = p; }
private:
    float m_JumpPower = 8.0f;
    float m_MoveSpeed = 5.0f;   // 水平移動速度
};