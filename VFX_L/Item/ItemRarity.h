// ============================================================
// ItemRarity.h
// 札の稀有度（ノーマル / レア / レジェンド）。
//   魔法・ルーン・設置枠・召喚物の全部が 3 段のどれか（割り当ての表は ItemDatabase.cpp の kRarityTable）。
//   四択（LevelUpSystem）と商店（ShopStock）は同じ引き方：**先に稀有度を引き、次にその段の札を引く**
//   （段毎の札の数で率が変わらないように）。1 枚がレア / レジェンドになる率はこのプレイの経過時間（分）で決める
//   （Odds の点の間は直線）。ふつうの宝箱は四択と同じ率、商店と金色の箱は時間に数分足して引く
//   見せ方：カードの面の色は魔法の種類のまま。報酬として出す時（四択・商店）にカードの外へ稀有度の色の縁を付ける（LevelUpUI::DrawCard）
// ============================================================
#pragma once
#include <cstdint>

enum class ItemRarity : uint8_t
{
    Common = 0,     // ノーマル
    Rare = 1,       // レア
    Legendary = 2,  // レジェンド
};

namespace Rarity
{
    constexpr int kCount = 3;

    inline const wchar_t* Name(ItemRarity r)
    {
        switch (r)
        {
        case ItemRarity::Rare:      return L"レア";
        case ItemRarity::Legendary: return L"レジェンド";
        default:                    return L"ノーマル";
        }
    }

    // 1 枚の札がレア / レジェンドになる率（LevelUpSystem / ShopStock が同じ物を持つ）。
    // 時間の点（kPoints 個）の間は直線、最初の点より前・最後の点より先はその点の値のまま。残りがノーマル
    struct Odds
    {
        static constexpr int kPoints = 4;
        float minute[kPoints] = { 0.0f, 10.0f, 20.0f, 30.0f };   // 始め / 1 面目の終わり / 2 面目の終わり / 3 面目の終わり
        float rare[kPoints]   = { 0.05f, 0.10f, 0.115f, 0.13f };
        float legend[kPoints] = { 0.00f, 0.01f, 0.03f, 0.06f };
    };

    constexpr float kStageMinutes = 10.0f;   // 1 面の制限時間（StageDirector::stageTime）
    constexpr float kPremiumMinutes = 5.0f;  // 商店・金色の箱（エリート・Boss）は時間にこれを足して引く

    // このプレイの経過時間（分）。pos = 何面目か（1 始まり）。面の時間切れの後は次の面の始めを超えない
    inline float RunMinutes(int pos, float stageSeconds)
    {
        const float m = stageSeconds / 60.0f;
        return (float)(pos - 1) * kStageMinutes + (m < kStageMinutes ? m : kStageMinutes);
    }

    inline float Interp(const float* x, const float* y, int n, float v)
    {
        if (v <= x[0]) return y[0];
        for (int i = 1; i < n; ++i)
            if (v <= x[i])
            {
                const float span = x[i] - x[i - 1];
                const float t = span > 0.0f ? (v - x[i - 1]) / span : 1.0f;
                return y[i - 1] + (y[i] - y[i - 1]) * t;
            }
        return y[n - 1];
    }

    // minutes（商店・金色の箱は足した後）での 1 枚の率 → out[ノーマル, レア, レジェンド]（合計 1）
    inline void Chances(float minutes, const Odds& o, float out[kCount])
    {
        float l = Interp(o.minute, o.legend, Odds::kPoints, minutes);
        float r = Interp(o.minute, o.rare, Odds::kPoints, minutes);
        l = l < 0.0f ? 0.0f : (l > 1.0f ? 1.0f : l);
        r = r < 0.0f ? 0.0f : (r > 1.0f - l ? 1.0f - l : r);
        out[(int)ItemRarity::Legendary] = l;
        out[(int)ItemRarity::Rare] = r;
        out[(int)ItemRarity::Common] = 1.0f - r - l;
    }

    // 段を引く：has[i] = その段にまだ引ける札が有る。無い段は外して残りの率で割り直す。u = 0〜1 の乱数。全部無ければ -1
    inline int RollTier(const float chance[kCount], const bool has[kCount], float u)
    {
        float total = 0.0f;
        for (int i = 0; i < kCount; ++i)
            if (has[i] && chance[i] > 0.0f) total += chance[i];
        if (total <= 0.0f)
        {
            for (int i = 0; i < kCount; ++i) if (has[i]) return i;   // 率 0 の段しか残っていない
            return -1;
        }
        float r = u * total;
        for (int i = 0; i < kCount; ++i)
        {
            if (!has[i] || chance[i] <= 0.0f) continue;
            r -= chance[i];
            if (r < 0.0f) return i;
        }
        for (int i = kCount - 1; i >= 0; --i) if (has[i] && chance[i] > 0.0f) return i;
        return -1;
    }
}
