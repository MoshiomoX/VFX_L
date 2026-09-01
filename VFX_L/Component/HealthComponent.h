// ============================================================
// HealthComponent.h
// 体力データ（純データ）。敵・プレイヤー共通。
// invincible はテスト用：ダメージは記録するが死亡しない。
// ============================================================
#pragma once

struct HealthComponent
{
    float current = 30.0f;
    float max = 30.0f;

    bool invincible = false;   // テスト用の無敵フラグ

    bool IsDead() const { return !invincible && current <= 0.0f; }
};