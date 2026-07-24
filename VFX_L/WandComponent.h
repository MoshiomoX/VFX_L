// ============================================================
// WandComponent.h
// 杖データ。マナは杖で共有し、出力源（spells）が奪い合う。
// spells はバックパック集約の結果。第1版は ImGui で手動編集する。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <vector>
#include "SpellID.h"

// 1つの出力源の最終属性（集約後の値）
struct SpellStats
{
    ItemID id = ItemID::Fireball;

    // --- 分裂（空間展開：同フレーム内に複数発）---
    int   projectileCount = 1;      // 一度の発射数
    float spreadAngle = 0.0f;   // 散布角（度）。count>1 の時に扇状へ

    // --- 二重釈放（時間展開：フレームを跨いで複数回）---
    int   castCount = 1;            // 1回の施法で何回撃つか
    float castDelay = 0.12f;        // 連射の間隔（秒）

    // --- リズム / コスト ---
    float castInterval = 0.5f;      // 施法間隔（秒）★プレイヤー調整
    float manaCost = 10.0f;     // 1回ぶんの消費（castCount 倍が総消費）

    // --- 投射物パラメータ ---
    float damage = 10.0f;
    float speed = 20.0f;
    float radius = 0.25f;
    float lifetime = 3.0f;

    // --- 実行時状態（WeaponSystem が更新）---
    float castTimer = 0.0f;      // 次の施法までの残り
    int   pendingCasts = 0;         // 二重釈放の残り発射回数
    float delayTimer = 0.0f;      // 次の連射までの残り
};

struct WandComponent
{
    // --- マナ（杖で共有）---
    float manaMax = 100.0f;
    float manaCurrent = 100.0f;
    float manaRegen = 25.0f;

    // --- 索敵範囲（杖共通）---
    float range = 15.0f;

    // 発射口オフセット
    DirectX::SimpleMath::Vector3 muzzleOffset = { 0.0f, 0.5f, 0.0f };

    // --- 出力源リスト（バックパック集約の結果）---
    std::vector<SpellStats> spells;
};