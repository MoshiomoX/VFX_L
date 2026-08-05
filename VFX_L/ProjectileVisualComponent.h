// ============================================================
// ProjectileVisualComponent.h
// 投射物の見た目（ビルボード芯）データ。
// ★衝突半径とは独立：見た目は大きく、判定は小さく保つ。
// ============================================================
#pragma once
#include <SimpleMath.h>

struct ProjectileVisualComponent
{
    float size = 0.8f;                                       // 見た目のサイズ
    DirectX::SimpleMath::Vector4 color = { 1, 0.7f, 0.3f, 1 };
    float stretch = 0.0f;                                    // 速度方向への引き伸ばし（0=円形）
};