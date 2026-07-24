// ============================================================
// PhysicsMath.h
// 衝突応答の純粋数学（無状態、自由関数）
// push-out（分離）、反射、滑走。ECS 非依存。
// ============================================================
#pragma once
#include <SimpleMath.h>

namespace PhysicsMath
{
    using DirectX::SimpleMath::Vector3;

    // --- push-out: めり込みを法線方向に押し出す量 ---
    //   contactNormal は「押し出す方向」、depth はめり込み深さ。
    inline Vector3 ResolvePenetration(const Vector3& normal, float depth)
    {
        return normal * depth;   // この分だけ位置を動かせば分離する
    }

    // --- Slide: 速度から法線方向成分を除去（面に沿って滑る）---
    //   壁: 水平法線 → 水平方向で止まり、他成分は保持
    //   床: 上向き法線 → 落下速度が消え、水平移動は保持
    inline Vector3 SlideVelocity(const Vector3& velocity, const Vector3& normal)
    {
        // v - (v・n) n  ： 法線方向の成分を引く
        float vn = velocity.Dot(normal);
        if (vn > 0.0f) return velocity;   // すでに離れる方向なら何もしない
        return velocity - normal * vn;
    }

    // --- Bounce: 速度を法線で反射（反発係数付き）---
    inline Vector3 ReflectVelocity(const Vector3& velocity, const Vector3& normal,
        float restitution)
    {
        float vn = velocity.Dot(normal);
        if (vn > 0.0f) return velocity;   // 離れる方向なら反射しない
        // 反射: v - (1+e)(v・n) n
        return velocity - normal * ((1.0f + restitution) * vn);
    }
}