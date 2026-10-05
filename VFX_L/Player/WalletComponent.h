// ============================================================
// WalletComponent.h
// 金貨（2026-10-04 ユーザー指定。Brotato 風に「経験値オーブ = お金」）。
//   ・拾った経験値 × goldPerExp だけ増える（雑魚 1 体 = 経験値 10 = 金貨 1。エリート・Boss は経験値の倍率でそのまま多い）。
//     端数は持ち越す（gold は float、使える・見せるのは切り捨て）
//   ・使い道：報酬の箱を開ける（RewardCrateSystem、開ける度に値上がり）、
//     レベルアップ / 箱の四択の選び直し（LevelUpSystem::Reroll、同じ四択の中で値上がり）
//   ・次の面へ持ち越す（g_RunCarry）
// ============================================================
#pragma once

struct WalletComponent
{
    float gold = 0.0f;           // 今の所持（端数込み）
    float goldPerExp = 0.1f;     // 経験値 1 あたり
    float earned = 0.0f;         // この局で手に入れた合計（記録用）
    float spent = 0.0f;          // この局で使った合計（記録用）

    int Coins() const { return (int)gold; }
    bool CanPay(int cost) const { return cost <= 0 || Coins() >= cost; }
    void Earn(float amount)
    {
        if (amount <= 0.0f) return;
        gold += amount;
        earned += amount;
    }
    // 払えたら true（足りなければ何もしない）
    bool Pay(int cost)
    {
        if (!CanPay(cost)) return false;
        if (cost > 0)
        {
            gold -= (float)cost;
            spent += (float)cost;
        }
        return true;
    }
};
