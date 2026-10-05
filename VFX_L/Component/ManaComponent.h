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
//   先に予約した方が勝つ。リザルトの順番は問わない。
// ============================================================
#pragma once

struct ManaComponent
{
    float max = 100.0f;
    float current = 100.0f;
    float regen = 25.0f;          // 毎秒の回復量（レベル 1 の時。能力アップ「魔力回復」はこれに掛ける）
    // 回復はレベルで伸びる（2026-10-04 ユーザー指定）：実際の回復 = regen × (1 + regenPerLevel × (レベル - 1))。
    // 魔法を増やしても MP が先に尽きて撃てず、撃破の速さが一局を通して頭打ちだった（自動テスト curve：平均 MP 4%）
    float regenPerLevel = 0.10f;
    float EffectiveRegen(int level) const
    {
        return regen * (1.0f + regenPerLevel * (float)(level > 1 ? level - 1 : 0));
    }

    // 今フレームの消費予約（ManaSystem が引き落として 0 に戻す）
    float pendingSpend = 0.0f;

    // ---- 魔力解放（Q / パッド Y、2026-10-01 ユーザー指定）----
    // surgeDuration 秒の間は魔力を消費しない（CanAfford は常に true、Reserve は積まない）。
    // 使った瞬間から surgeCooldown 秒は使えない（2026-10-04 ユーザー指定で 30 → 20 秒）。時間は ManaSystem が進める
    float surgeDuration = 3.0f;
    float surgeCooldown = 20.0f;
    float surgeTime = 0.0f;           // 残り（> 0 の間は無限）
    float surgeCooldownLeft = 0.0f;   // 次に使えるまで
    // 解放中の強化（2026-10-02 ユーザー指定、WeaponSystem が見る）:
    //   詠唱 = 発動間隔・上級魔法のクールダウン・連発の間・光線の溜めの計時がこの倍の速さで進む
    //   持続 = 解放中に撃った魔法の「持続する範囲」（毒の池・光線）がこの倍だけ長く残る（撃った時に決まる）
    float surgeCastSpeed = 1.5f;
    float surgeDurationMul = 1.5f;
    bool SurgeActive() const { return surgeTime > 0.0f; }
    float CastSpeed() const { return SurgeActive() ? surgeCastSpeed : 1.0f; }
    float DurationMul() const { return SurgeActive() ? surgeDurationMul : 1.0f; }
    // 使えれば始めて true
    bool TryStartSurge()
    {
        if (surgeCooldownLeft > 0.0f) return false;
        surgeTime = surgeDuration;
        surgeCooldownLeft = surgeCooldown;
        return true;
    }

    // 予約済みを差し引いた上で払えるか（解放中は常に払える）
    bool CanAfford(float cost) const { return SurgeActive() || current - pendingSpend >= cost; }

    // 消費を予約する。払えるかは呼ぶ側が CanAfford で確認しておく（解放中は積まない = 減らない）
    void Reserve(float cost) { if (!SurgeActive()) pendingSpend += cost; }
};