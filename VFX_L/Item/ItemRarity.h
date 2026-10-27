// ============================================================
// ItemRarity.h
// 札の稀有度（2026-10-28。ユーザー：設置枠を小さくしたら上級魔法が置きにくくなった →
// 「上級魔法をやめて稀有度で並べる」「全部の札に稀有度」）。
//   以前の上級魔法（メテオ・魔導光線・猛毒の沼・アイススパイク・竜巻）は前提の基本魔法なしで単独で撃つ、
//   珍しい（レア / レジェンド）魔法になった。魔法・ルーン・設置枠・召喚物の全部が 3 段のどれか
//   （割り当ての表は ItemDatabase.cpp の kRarityTable）。
//   四択（LevelUpSystem）と商店（ShopStock）は同じ式で引く：ノーマル 1 / レア 0.30 / レジェンド 0.06 を基準に、
//   先へ進むほど（boost）珍しい札の重みが伸びる。boost = (レベル − 1) / 29 + 0.5 × (この局の何面目 − 1)（+ 宝箱 0.5）
//   ＝ 1 面目の終わり（レベル 30）で 1。
//   見せ方：カードの面の色は魔法の種類のまま（ユーザー「面の色は種類で分けたい」）。
//   報酬として出す時（四択・商店）にカードの外へ稀有度の色の縁を付ける（LevelUpUI::DrawCard）
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

    // 引く時の重み（LevelUpSystem / ShopStock が同じ物を持つ）
    struct Odds
    {
        float base[kCount] = { 1.0f, 0.30f, 0.06f };   // boost 0（局の始め）の重み
        float grow[kCount] = { 0.0f, 1.5f, 4.0f };     // boost 1 で base × (1 + grow) になる
    };

    inline float Weight(ItemRarity r, float boost, const Odds& o)
    {
        const int i = (int)r;
        if (i < 0 || i >= kCount) return 0.0f;
        const float b = boost < 0.0f ? 0.0f : boost;
        return o.base[i] * (1.0f + o.grow[i] * b);
    }

    // 先へ進んだ度合い（四択）：レベル 30（1 面目の終わり）で 1、局の 2 面目以降は 0.5 ずつ足す
    inline float Boost(int level, int runPos, float extra = 0.0f)
    {
        const float lv = level > 1 ? (float)(level - 1) / 29.0f : 0.0f;
        const float pos = runPos > 1 ? 0.5f * (float)(runPos - 1) : 0.0f;
        return (lv < 1.5f ? lv : 1.5f) + pos + extra;
    }
}
