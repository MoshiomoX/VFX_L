// ============================================================
// ShieldComponent.h
// シールド（2026-10-07 ユーザー指定：「Megabonk と同じでいい」）。
//   ・HP より先にダメージを受ける。シールドが少しでも残っていれば、その一撃は全部シールドで受ける
//     （Megabonk：一撃でシールドと HP の両方は削れない。溢れた分は消える）
//   ・最後に被弾してから rechargeDelay 秒（Megabonk の 5 秒）経つと、refillTime 秒かけて満タンまで戻る
//     （戻る速さは Megabonk の数値が見つからなかったので自前で決めた）
//   ・初期値 25（ユーザー指定）、レベルアップの能力カード「最大シールド +25」で増える（Megabonk のシールドの書と同じ量）
//   ・次の面へ持ち越す（g_RunCarry。面の初めは満タン）
// 被弾の窓口は PlayerStateSystem::TryApplyHit（無敵時間と同じ所）、回復は PlayerStateSystem::Update
// ============================================================
#pragma once
#include <cstdint>

struct ShieldComponent
{
    float max = 25.0f;
    float current = 25.0f;
    float rechargeDelay = 5.0f;   // 最後の被弾からこの秒数で戻り始める（Megabonk）
    float refillTime = 2.0f;      // 0 → 満タンまでの秒（上限の割合で戻す。上限が大きいほど 1 秒で戻る量も多い）
    float sinceHit = 0.0f;        // 最後に被弾してからの秒（シールドで受けても HP で受けても 0 に戻る）

    // 反応（エフェクト・音）の検出用の累計。減らさない
    uint32_t hits = 0;            // シールドで受けた回数
    uint32_t breaks = 0;          // シールドが 0 になった回数

    bool Full() const { return current >= max; }
    bool Recharging() const { return current < max && sinceHit >= rechargeDelay; }

    // 被弾を受けた（無敵で弾かれた分は呼ばない）。シールドで受けたら true（HP は減らさない）
    bool Absorb(float damage)
    {
        sinceHit = 0.0f;
        if (current <= 0.0f || damage <= 0.0f) return false;
        current -= damage;
        ++hits;
        if (current <= 0.0f)
        {
            current = 0.0f;   // 溢れた分は消える
            ++breaks;
        }
        return true;
    }

    void Tick(float dt)
    {
        sinceHit += dt;
        if (current >= max)
        {
            current = max;   // 上限を下げた時（デバッグ）も丸める
            return;
        }
        if (sinceHit < rechargeDelay) return;
        const float rate = (refillTime > 0.0f) ? max / refillTime : max * 1000.0f;
        current += rate * dt;
        if (current > max) current = max;
    }
};
