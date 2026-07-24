// ============================================================
// WandComponent.h
// 杖データ（純データ）。マナは杖ごとに持つ。
// castInterval がプレイヤーの調整する唯一のリズムパラメータ。
// ============================================================
#pragma once
#include <SimpleMath.h>

struct WandComponent
{
    // --- リズム ---
    float castInterval = 0.4f;   // 発射間隔（秒）★プレイヤー調整
    float castTimer = 0.0f;   // 内部計時

    // --- マナ ---
    float manaMax = 100.0f;
    float manaCurrent = 100.0f;
    float manaRegen = 25.0f;   // 毎秒回復量
    float manaCost = 10.0f;   // 1発の消費（後で法術定義から取る）

    // --- 索敵 ---
    float range = 15.0f;

    // --- 発射する投射物のパラメータ ---
    float projectileSpeed = 20.0f;
    float projectileRadius = 0.25f;
    float projectileDamage = 10.0f;
    float projectileLifetime = 3.0f;

    // 発射口オフセット（Entity 中心から）
    DirectX::SimpleMath::Vector3 muzzleOffset = { 0.0f, 0.5f, 0.0f };
};