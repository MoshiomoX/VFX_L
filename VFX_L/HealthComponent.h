// ============================================================
// HealthComponent.h
// 体力データ（純データ）。敵・プレイヤー共通。
// ============================================================
#pragma once

struct HealthComponent
{
    float current = 30.0f;
    float max = 30.0f;

    bool IsDead() const { return current <= 0.0f; }
};