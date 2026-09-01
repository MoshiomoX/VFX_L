// ============================================================
// ManaComponent.h
// 魔力データ（純データ）。現時点では HUD の表示のみに使う。
//
// 将来の消費に備えた設計：
//   詠唱側は CanAfford / TrySpend だけを呼ぶ。
//   数値を直接書き換える場所を増やさないため。
//   regenPerSec は自然回復の係数。回復を担当する System を
//   実装する時に「誰が書くか」を決める（一つのデータは一つの System）。
// ============================================================
#pragma once

struct ManaComponent
{
    float current = 50.0f;
    float max = 50.0f;

    // 自然回復（毎秒）。回復 System 実装までは未使用
    float regenPerSec = 5.0f;

    bool CanAfford(float cost) const { return current >= cost; }

    // 足りれば消費して true。詠唱側はこれだけを使う
    bool TrySpend(float cost)
    {
        if (current < cost) return false;
        current -= cost;
        return true;
    }

    // 回復処理の後などに範囲へ収める
    void ClampToRange()
    {
        if (current > max) current = max;
        if (current < 0.0f) current = 0.0f;
    }
};
