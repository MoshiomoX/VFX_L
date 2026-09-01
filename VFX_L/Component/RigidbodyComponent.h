// ============================================================
// RigidbodyComponent.h
// 物理挙動データ（純データ）。プレイヤー/敵/投射物 共通。
// 「誰が持つか」ではなく「どんな物理挙動か」で定義する。
// ============================================================
#pragma once
#include <SimpleMath.h>

// 衝突応答モード
enum class ResponseMode
{
    Slide,   // 面に沿って滑る（キャラクター向け：壁で止まり床に立つ）
    Bounce,  // 反射する（投射物向け）
    Stop,    // 速度を殺して止まる
};

struct RigidbodyComponent
{
    DirectX::SimpleMath::Vector3 velocity = { 0, 0, 0 };  // 速度

    bool  useGravity = true;    // 重力を受けるか
    bool  isStatic = false;   // 静的（地形など、動かない・押し出されない）
    bool  isGrounded = false;   // 接地しているか（PhysicsSystem が更新）

    ResponseMode response = ResponseMode::Slide;
    float restitution = 0.3f;    // 反発係数（Bounce 用、0=無反発 1=完全反発）
};