// ============================================================
// ShopStock.h
// 面と面の間の商店の品揃えと売買（2026-10-15。金貨の「第二歩」）。
//   画面は Scene/ShopScene が持つ。ここは「何を並べるか・いくらか・買ったら何が起きるか」だけで、
//   相手は面をまたぐ引き継ぎ（g_RunCarry）：金貨は wallet から払い、魔法・ルーン・枠は spellbook へ、
//   能力値の札は carry の各部品にその場で効かせる（次の面の始まりに ApplyRunCarry が写す）。
//
// 品揃え（まずは簡単に。ユーザー 10-15「売る物は適当に少し置けばいい」）：
//   [0..3] 魔法・ルーン・召喚物から無作為に 4 つ（同じ物は並ばない。上級魔法は前提の基本魔法を 1 つでも持っている時だけ）
//   [4]    設置枠を 1 つ（種類は無作為）
//   [5]    能力値の札を 1 つ（無作為）
// 値段は種類で固定（PriceOf）。品揃えの引き直しは 15 + 10 × 回数（四択の引き直しと同じ上がり方）
// ============================================================
#pragma once
#include "SpellID.h"
#include <vector>

struct RunCarry;
struct StatItemDef;

class ShopStock
{
public:
    static constexpr int kSlots = 6;

    struct Slot
    {
        ItemID id = ItemID::Unknown;
        int    price = 0;
        bool   sold = false;
    };

    // 品揃えを作り直す（引き直しの回数は保つ）
    void Generate(const RunCarry& carry);

    // 買う。払えない・売り切れ・空なら false（何も変えない）
    bool Buy(RunCarry& carry, int slot);

    // 引き直す。払えなければ false
    bool Reroll(RunCarry& carry);
    int  RerollCost() const;   // (base + step × 回数) × 面の倍率
    int  RerollCount() const { return m_RerollCount; }

    const Slot& At(int i) const { return m_Slots[i]; }
    int Count() const { return (int)m_Slots.size(); }

    // 種類で決まる値段 × 面の倍率（並べる時に使う。未登録の ID は 0）
    int PriceOf(ItemID id) const;
    // 面の倍率（プレイの 2 面目へ向かう商店 = 1、3 面目へ = stagePriceGrowth）。
    // Generate が carry.stage - carry.startStage（プレイの中の何面目か）から決める
    float StageMul() const { return m_StageMul; }

    // ---- 調整値 ----
    int rerollBaseCost = 15;
    int rerollStepCost = 10;
    float stagePriceGrowth = 1.5f;   // 次の面が 1 つ進むごとに値段 ×（後の商店ほど高い。引き直しも同じ倍率）
    // 魔法・ルーン・召喚物の欄（[0..3]）の出やすさ。基本魔法 1 を基準に
    float weightBasic = 1.0f;
    float weightRune = 0.8f;
    float weightAdvanced = 0.7f;   // 上級魔法（前提を持っている時だけ並ぶ）
    float weightSummon = 0.5f;
    float weightOwnedMul = 0.6f;   // 既に魔法書にある物はこれを掛ける（同じ物ばかり並ばないように）
    int priceBasic = 30;      // 基本魔法（弾・範囲）
    int priceAdvanced = 60;   // 上級魔法（前提が要る物）
    int priceRune = 40;       // ルーン
    int priceSummon = 60;     // 召喚物（水晶玉）
    int priceFrame = 40;      // 設置枠
    int priceStat = 25;       // 能力値の札

private:
    // 能力値の札を引き継ぎの部品へ効かせる（LevelUpSystem::ApplyStat の RunCarry 版。
    // HP / MP / シールドの今の値は次の面の始まりに満タンにされるので上限だけ）
    static void ApplyStatToCarry(RunCarry& carry, const StatItemDef& stat);

    std::vector<Slot> m_Slots;
    int m_RerollCount = 0;
    float m_StageMul = 1.0f;
};
