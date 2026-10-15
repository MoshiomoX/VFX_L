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
    if (ItemDatabase::GetStat(id)) return priceStat;
    const ItemCommon* c = ItemDatabase::GetCommon(id);
    if (!c) return 0;
    switch (c->category)
    {
    case ItemCategory::Projectile:
    case ItemCategory::Area:     return c->triggeredBy.empty() ? priceBasic : priceAdvanced;
    case ItemCategory::Function: return priceRune;
    case ItemCategory::Summon:   return priceSummon;
    case ItemCategory::Frame:    return priceFrame;
    default:                     return priceBasic;
    }
}

// ============================================================
// 品揃え
// ============================================================
void ShopStock::Generate(const RunCarry& carry)
{
    m_Slots.clear();
    const SpellbookComponent& book = carry.spellbook;

    // [0..3] 魔法・ルーン・召喚物（LevelUpSystem::FillChoices と同じ絞り方。設置枠は別の欄に出す）
    std::vector<ItemID> pool, frames;
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) continue;
        if (c->category == ItemCategory::Frame) { frames.push_back(id); continue; }
        if (!c->triggeredBy.empty())
        {
            bool hasAny = false;
            for (ItemID need : c->triggeredBy)
                if (book.HasLearned(need)) { hasAny = true; break; }
            if (!hasAny) continue;
        }
        pool.push_back(id);
    }
    for (int i = 0; i < 4 && !pool.empty(); ++i)
    {
        const int pick = RandBelow((int)pool.size());
        m_Slots.push_back({ pool[pick], PriceOf(pool[pick]), false });
        pool.erase(pool.begin() + pick);
    }

    // [4] 設置枠
    if (!frames.empty())
    {
        const ItemID id = frames[RandBelow((int)frames.size())];
        m_Slots.push_back({ id, PriceOf(id), false });
    }

    // [5] 能力値の札
    const auto& stats = ItemDatabase::GetLevelUpOnlyIDs();
    if (!stats.empty())
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
