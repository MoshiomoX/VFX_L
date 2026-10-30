// ============================================================
// ShopStock.cpp
// ============================================================
#include "UI/ShopStock.h"
#include "Scene/RunResult.h"
#include "Item/ItemDatabase.h"

namespace
{
    int RandBelow(int n)
    {
        if (n <= 1) return 0;
        return (int)((float)rand() / ((float)RAND_MAX + 1.0f) * (float)n);
    }
}

// ============================================================
// 値段（種類で固定）
// ============================================================
int ShopStock::PriceOf(ItemID id) const
{
    int base = 0;
    if (ItemDatabase::GetStat(id)) base = priceStat;
    else if (const ItemCommon* c = ItemDatabase::GetCommon(id))
    {
        // 稀有度で決める（2026-10-28）。設置枠は枠ごとの倍率（2x3 など大きい物は少し高い）
        base = priceByRarity[std::clamp((int)c->rarity, 0, Rarity::kCount - 1)];
        if (const FrameItemDef* fd = ItemDatabase::GetFrame(id))
            base = (int)std::lround((float)base * fd->priceMul);
    }
    return (int)std::lround((float)base * m_StageMul);
}

int ShopStock::RerollCost() const
{
    return (int)std::lround((float)(rerollBaseCost + rerollStepCost * m_RerollCount) * m_StageMul);
}

// ============================================================
// 品揃え
// ============================================================
void ShopStock::Generate(const RunCarry& carry)
{
    m_Slots.clear();
    const SpellbookComponent& book = carry.spellbook;

    // 面の倍率：このプレイの何面目へ向かう商店か（難度の StageConfig::PosDifficulty と同じ数え方）で決める。
    // プレイの 2 面目へ向かう商店が 1、以降 1 面ごとに stagePriceGrowth 倍。
    // （以前は carry.stage = マップの番号から数えていて、砂漠から始めた Tier 2 の最初の商店がもう 1.5 倍だった。10-26 から carry.pos）
    const int posNext = carry.pos - 1;   // 次の面がこのプレイの何面目か（0 = 1 面目）
    const int steps = (std::max)(0, posNext - 1);
    m_StageMul = std::pow((std::max)(1.0f, stagePriceGrowth), (float)steps);

    // [0..3] 魔法・ルーン・召喚物（設置枠は別の欄に出す）、[4] 設置枠。どちらも先に稀有度の段を引き（率はレベル + shopLevelBonus）、
    // その段の中で重みに比例して引く（持っている物は下げる。枠は FrameItemDef::weight）。2026-10-29 から（以前は稀有度の重みを掛けて 1 つの池）
    struct Entry { ItemID id; float weight; };
    std::vector<Entry> pools[Rarity::kCount], framePools[Rarity::kCount];
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) continue;
        const int tier = std::clamp((int)c->rarity, 0, Rarity::kCount - 1);
        if (c->category == ItemCategory::Frame)
        {
            const FrameItemDef* fd = ItemDatabase::GetFrame(id);
            framePools[tier].push_back({ id, fd ? (std::max)(fd->weight, 0.0f) : 1.0f });
            continue;
        }
        pools[tier].push_back({ id, book.HasLearned(id) ? weightOwnedMul : 1.0f });
    }
    float chance[Rarity::kCount];
    Rarity::Chances((float)carry.level.level + shopLevelBonus, rarityOdds, chance);
    // 段を引いてその段の中から 1 つ（引いた物は外す）。何も無ければ Unknown
    auto draw = [&](std::vector<Entry>* tiers) -> ItemID
        {
            bool has[Rarity::kCount];
            for (int i = 0; i < Rarity::kCount; ++i)
            {
                float t = 0.0f;
                for (const Entry& e : tiers[i]) t += (std::max)(e.weight, 0.0f);
                has[i] = t > 0.0f;
            }
            const int tier = Rarity::RollTier(chance, has, (float)rand() / ((float)RAND_MAX + 1.0f));
            if (tier < 0) return ItemID::Unknown;
            std::vector<Entry>& pool = tiers[tier];
            float total = 0.0f;
            for (const Entry& e : pool) total += (std::max)(e.weight, 0.0f);
            float r = (float)rand() / ((float)RAND_MAX + 1.0f) * total;
            size_t pick = pool.size() - 1;
            for (size_t j = 0; j < pool.size(); ++j)
            {
                r -= (std::max)(pool[j].weight, 0.0f);
                if (r < 0.0f) { pick = j; break; }
            }
            const ItemID id = pool[pick].id;
            pool.erase(pool.begin() + (ptrdiff_t)pick);
            return id;
        };
    for (int i = 0; i < 4; ++i)
    {
        const ItemID id = draw(pools);
        if (id == ItemID::Unknown) break;
        m_Slots.push_back({ id, PriceOf(id), false });
    }
    if (const ItemID id = draw(framePools); id != ItemID::Unknown)
        m_Slots.push_back({ id, PriceOf(id), false });

    // [5] 能力値の札（statSlot = false の間は出さない）
    const auto& stats = ItemDatabase::GetLevelUpOnlyIDs();
    if (statSlot && !stats.empty())
    {
        const ItemID id = stats[RandBelow((int)stats.size())];
        m_Slots.push_back({ id, PriceOf(id), false });
    }
}

// ============================================================
// 買う
// ============================================================
bool ShopStock::Buy(RunCarry& carry, int slot)
{
    if (slot < 0 || slot >= (int)m_Slots.size()) return false;
    Slot& s = m_Slots[slot];
    if (s.sold || s.id == ItemID::Unknown) return false;
    if (!carry.wallet.CanPay(s.price)) return false;

    if (const StatItemDef* stat = ItemDatabase::GetStat(s.id))
    {
        carry.wallet.Pay(s.price);
        ApplyStatToCarry(carry, *stat);
    }
    else
    {
        if (!ItemDatabase::GetCommon(s.id)) return false;
        carry.wallet.Pay(s.price);
        carry.spellbook.Learn(s.id, 1);   // 魔法書へ（次の面でバックパックに置く）
    }
    s.sold = true;
    return true;
}

// ============================================================
// 引き直し
// ============================================================
bool ShopStock::Reroll(RunCarry& carry)
{
    const int cost = RerollCost();
    if (!carry.wallet.CanPay(cost)) return false;
    carry.wallet.Pay(cost);
    ++m_RerollCount;
    Generate(carry);
    return true;
}

// ============================================================
// 能力値の札（RunCarry 版）
// ============================================================
void ShopStock::ApplyStatToCarry(RunCarry& carry, const StatItemDef& stat)
{
    auto scale = [&](float v) { return stat.percent ? v * (1.0f + stat.amount) : v + stat.amount; };
    switch (stat.kind)
    {
    case StatKind::MaxHealth:  carry.health.max += stat.amount; break;
    case StatKind::MaxMana:    carry.mana.max += stat.amount; break;
    case StatKind::MoveSpeed:  carry.stats.moveSpeed = scale(carry.stats.moveSpeed); break;
    case StatKind::JumpPower:  carry.stats.jumpPower = scale(carry.stats.jumpPower); break;
    case StatKind::ManaRegen:  carry.mana.regen = scale(carry.mana.regen); break;
    case StatKind::JumpCount:  carry.stats.extraJumps += (int)(stat.amount + 0.5f); break;
    case StatKind::SpellPower: carry.stats.spellPower = scale(carry.stats.spellPower); break;   // 次の面の集約で掛かる
    case StatKind::Shield:     carry.shield.max += stat.amount; break;
    }
}
