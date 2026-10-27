// ============================================================
// ShopStock.h
// 面と面の間の商店の品揃えと売買（2026-10-15。金貨の「第二歩」）。
//   画面は Scene/ShopScene が持つ。ここは「何を並べるか・いくらか・買ったら何が起きるか」だけで、
//   相手は面をまたぐ引き継ぎ（g_RunCarry）：金貨は wallet から払い、魔法・ルーン・枠は spellbook へ、
//   能力値の札は carry の各部品にその場で効かせる（次の面の始まりに ApplyRunCarry が写す）。
//
// 品揃え（まずは簡単に。ユーザー 10-15「売る物は適当に少し置けばいい」）：
//   [0..3] 魔法・ルーン・召喚物から無作為に 4 つ（同じ物は並ばない。2026-10-28 から稀有度の重みで引く）
//   [4]    設置枠を 1 つ（種類は無作為）
//   [5]    能力値の札を 1 つ（無作為）→ 2026-10-27 ユーザー「商店の能力値の札も外す」で statSlot = false（出さない）
// 値段は稀有度で固定（PriceOf。設置枠は枠ごとの priceMul を掛ける。2026-10-28 までは種類で決めていた）。品揃えの引き直しは 60 + 40 × 回数。
// 2026-10-27 ユーザー「商店が安すぎて一度入ると無敵」→ 値段・引き直しを全部 4 倍
// ============================================================
#pragma once
#include "SpellID.h"
#include "Item/ItemRarity.h"
#include <vector>

struct RunCarry;
struct StatItemDef;

class ShopStock
{
public:
    static constexpr int kSlots = 5;   // 並ぶ欄の数（一覧の高さ）。能力値の札（statSlot）を戻すなら 6 に

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
    // Generate が carry.pos（プレイの中の何面目か）から決める
    float StageMul() const { return m_StageMul; }

    // ---- 調整値 ----
    int rerollBaseCost = 60;    // 10-27 に 15 → 60（全部 4 倍）
    int rerollStepCost = 40;
    float stagePriceGrowth = 1.5f;   // 次の面が 1 つ進むごとに値段 ×（後の商店ほど高い。引き直しも同じ倍率）
    // 出やすさと値段は稀有度で決める（2026-10-28、ItemRarity.h。以前は 基本魔法 / ルーン / 上級 / 召喚物 / 枠 の種類毎）。
    // 重み = 稀有度の重み（商店は 1 面目を終えた所なので boost = shopRarityBoost + 0.5 × (次の面 − 2)）
    Rarity::Odds rarityOdds;
    float shopRarityBoost = 1.0f;
    float weightOwnedMul = 0.6f;   // 既に魔法書にある物はこれを掛ける（同じ物ばかり並ばないように）
    int priceByRarity[Rarity::kCount] = { 120, 200, 320 };   // ノーマル / レア / レジェンド（設置枠は FrameItemDef::priceMul を掛ける）
    int priceStat = 100;      // 能力値の札
    bool statSlot = false;    // [5] 能力値の札を並べるか（2026-10-27 に外した。四択の LevelUpSystem::statCards と同じ）

private:
    // 能力値の札を引き継ぎの部品へ効かせる（LevelUpSystem::ApplyStat の RunCarry 版。
    // HP / MP / シールドの今の値は次の面の始まりに満タンにされるので上限だけ）
    static void ApplyStatToCarry(RunCarry& carry, const StatItemDef& stat);

    std::vector<Slot> m_Slots;
    int m_RerollCount = 0;
    float m_StageMul = 1.0f;
};
