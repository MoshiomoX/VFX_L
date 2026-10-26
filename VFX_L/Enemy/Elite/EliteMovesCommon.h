// ============================================================
// EliteMovesCommon.h
// EliteMoves.cpp / EliteMovesRuins.cpp の内側だけで使う小さな計算（向き・水平距離・回し）。
// 2026-10-26 に EliteMoves.cpp（550 行）から遺跡の 2 型を EliteMovesRuins.cpp へ分けた時に作った
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>

namespace EliteMovesDetail
{
    using DirectX::SimpleMath::Vector2;
    using DirectX::SimpleMath::Vector3;

    inline float YawOf(const Vector2& d) { return std::atan2(d.x, d.y); }
    inline Vector2 DirOf(float yaw) { return Vector2(std::sin(yaw), std::cos(yaw)); }
    inline Vector2 FlatTo(const Vector3& from, const Vector3& to)
    {
        Vector2 d(to.x - from.x, to.z - from.z);
        const float l = d.Length();
        return (l > 1e-4f) ? d / l : Vector2(0.0f, 1.0f);
    }
    inline float FlatDist(const Vector3& a, const Vector3& b)
    {
        const float dx = a.x - b.x, dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    }
    // a から b へ最大 maxStep（ラジアン）だけ回す
    inline float TurnToward(float a, float b, float maxStep)
    {
        float d = b - a;
        while (d > 3.14159265f) d -= 6.2831853f;
        while (d < -3.14159265f) d += 6.2831853f;
        return a + std::clamp(d, -maxStep, maxStep);
    }
}
