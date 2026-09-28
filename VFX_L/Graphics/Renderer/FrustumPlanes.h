// ============================================================
// FrustumPlanes.h
// view * proj（行ベクトル規約）から視錐台の 6 平面を取る（法線は内向き・正規化済み）。
// 切り取り空間の条件 -w<=x<=w, -w<=y<=w, 0<=z<=w をそのまま平面にするので、右手系・左手系を問わない。
// 並び: 0 左 / 1 右 / 2 下 / 3 上 / 4 近 / 5 遠。点 p が内側 = dot(n, p) + d >= 0
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cmath>

inline void ExtractFrustumPlanes(const DirectX::SimpleMath::Matrix& m, DirectX::SimpleMath::Vector4 out[6])
{
    using DirectX::SimpleMath::Vector4;
    const Vector4 c0(m._11, m._21, m._31, m._41);
    const Vector4 c1(m._12, m._22, m._32, m._42);
    const Vector4 c2(m._13, m._23, m._33, m._43);
    const Vector4 c3(m._14, m._24, m._34, m._44);
    out[0] = c3 + c0;   // 左
    out[1] = c3 - c0;   // 右
    out[2] = c3 + c1;   // 下
    out[3] = c3 - c1;   // 上
    out[4] = c2;        // 近
    out[5] = c3 - c2;   // 遠
    for (int i = 0; i < 6; ++i)
    {
        const float len = std::sqrt(out[i].x * out[i].x + out[i].y * out[i].y + out[i].z * out[i].z);
        if (len > 1e-6f) out[i] /= len;
    }
}
