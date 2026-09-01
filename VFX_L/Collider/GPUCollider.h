// ============================================================
// GPUCollider.h
// GPU 側衝突体構造体（HLSL の GPUCollider と厳密一致）
// 16バイト境界を守る（float3 の後に必ず float を1つ入れて詰める）
// static_assert(sizeof(GPUCollider) == 48, "HLSL 側と不一致");
// ============================================================
#pragma once
#include <SimpleMath.h>

// 形状タイプ（HLSL 側と数値を一致させること）
enum class GPUColliderType : int
{
    Sphere = 0,
    AABB = 1,   // 第3段階
    Capsule = 2,   // 第3段階
};

// 48バイト（float4 × 3）。将来の形状もこの枠に収める。
struct GPUCollider
{
    // --- 16バイト目 ---
    DirectX::SimpleMath::Vector3 center;   // Sphere:中心 / AABB:min / Capsule:始点
    float radius = 0.0f;                   // Sphere/Capsule:半径

    // --- 16バイト目 ---
    DirectX::SimpleMath::Vector3 extent;   // AABB:max / Capsule:終点（Sphere:未使用）
    int type = 0;                          // GPUColliderType

    // --- 16バイト目（将来用・現状ゼロ埋め）---
    float _pad0 = 0.0f;
    float _pad1 = 0.0f;
    float _pad2 = 0.0f;
    float _pad3 = 0.0f;
};