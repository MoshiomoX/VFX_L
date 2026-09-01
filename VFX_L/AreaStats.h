// ============================================================
// AreaStats.h
// AOE 型魔法の実行時パラメータ。
// ※SpellStats（飛行物用）とは完全に別系統。
//   範囲・持続・tick という時間軸の概念が飛行物と根本的に違うため、
//   同じ構造体に混ぜず独立させる。
// ============================================================
#pragma once
#include "SpellID.h"

struct AreaStats
{
    ItemID id = ItemID::Fireball;

    // --- 範囲 ---
    float radius = 3.0f;          // 影響半径

    // --- 時間 ---
    float duration = 2.0f;    // 発生してから消えるまで
    float tickInterval = 0.25f;   // 何秒ごとにダメージを与えるか

    // --- 威力 ---
    float damagePerTick = 3.0f;   // 1 tick あたりのダメージ

    // --- リズム / コスト ---
    float castInterval = 2.0f;    // 発動間隔
    float manaCost = 20.0f;

    // --- 発生位置の指定方法 ---
    // 敵の足元に出すか、プレイヤー中心に出すか
    bool spawnAtTarget = true;

    // --- 実行時状態（AreaSystem が更新）---
    float castTimer = 0.0f;
};