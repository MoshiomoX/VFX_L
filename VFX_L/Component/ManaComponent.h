// ============================================================
// ManaComponent.h
// 魔力（純データ）。使い手が持つ。杖の属性ではない。
//
// 書き手は ManaSystem だけ。
//   消費したい側（WeaponSystem 等）は Reserve() で予約を積むだけで、
//   current は触らない。予約は ManaSystem が毎フレーム末に引き落とす。
//   ※粒子の Submit → Flush と同じ形。書き手を1つに絞るため。
//
// 同一フレーム内で複数の出力源が奪い合う場合:
//   CanAfford() は予約済みを差し引いて判定するので、
//   先に予約した方が勝つ。結算の順番は問わない。
// ============================================================
#pragma once

struct ManaComponent
{
    float max = 100.0f;
    float current = 100.0f;
    float regen = 25.0f;          // 毎秒の回復量

    // 今フレームの消費予約（ManaSystem が引き落として 0 に戻す）
    float pendingSpend = 0.0f;

    // 予約済みを差し引いた上で払えるか
    bool CanAfford(float cost) const { return current - pendingSpend >= cost; }

    // 消費を予約する。払えるかは呼ぶ側が CanAfford で確認しておく
    void Reserve(float cost) { pendingSpend += cost; }
};