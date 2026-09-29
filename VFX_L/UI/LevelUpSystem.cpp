// ============================================================
// LevelUpSystem.cpp
// ============================================================
#include "UI/LevelUpSystem.h"
#include "ECS/Registry.h"
#include "Player/LevelComponent.h"
#include "Component/SpellbookComponent.h"
#include "Player/PlayerTag.h"
#include "Item/ItemDatabase.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Player/PlayerStatsComponent.h"
#include "ECS/View.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

void LevelUpSystem::Update(Registry& reg)
{
    // 走査中に Component を足したり消したりはしないが、
    // 抽選で他のコンポーネントを読むので、対象を集めてから処理する。
    std::vector<Entity> targets;

    reg.CreateView<LevelComponent, PlayerTag>()
        .Each([&](Entity e, LevelComponent& lv, PlayerTag&)
            {
                // 選択待ちの間は次のレベルアップを処理しない。
                // 候補が上書きされて、選ぶ前に消えてしまうため。
                if (lv.IsChoosing()) return;
                if (!lv.CanLevelUp())  return;

                targets.push_back(e);
            });

    for (Entity e : targets)
        RollChoices(reg, e);
}

// ============================================================
// 候補の抽選
//
// 池は登録済みの全 ID。習得済みでも候補に入る。
// 同じ魔法をもう1つ持てば、2つ並べて互いに影響させる構築が成立する。
// 設置枠も同じ池に入れる。それが「置ける領域を広げる」報酬になる。
//
// 重複は許さない。3つとも違うものを見せた方が選ぶ意味がある。
// ============================================================
void LevelUpSystem::RollChoices(Registry& reg, Entity player)
{
    auto& lv = reg.Get<LevelComponent>(player);

    if (FillChoices(lv) == 0)
    {
        // 候補が1つも無いなら、レベルだけ上げて先へ進める。
        // ここで止まると経験値が溜まり続けて動かなくなる。
        lv.ConsumeLevelUp();
        ++m_TotalLevelUps;
        std::cout << "[LevelUp] no candidate available" << std::endl;
        return;
    }

    // 候補を出した時点でレベルを上げる。
    // 選び終わってから上げる方式にすると、
    // 選択中に経験値が入った場合の扱いが面倒になる。
    lv.ConsumeLevelUp();
    ++m_TotalLevelUps;

    std::cout << "[LevelUp] level " << lv.level
        << " : " << lv.pendingChoices.size() << " choices" << std::endl;
}

// ============================================================
// 報酬の三択（レベルは上がらない。報酬の箱など）
// 選び方・確定（Choose）・画面は升級とまったく同じ。
// 既に選択待ちなら出さない（候補を上書きすると選ぶ前に消える）
// ============================================================
bool LevelUpSystem::OfferChoices(Registry& reg, Entity player)
{
    if (!reg.IsValid(player) || !reg.Has<LevelComponent>(player)) return false;

    auto& lv = reg.Get<LevelComponent>(player);
    if (lv.IsChoosing()) return false;
    if (FillChoices(lv) == 0) return false;

    ++m_TotalOffers;
    std::cout << "[LevelUp] reward offer : " << lv.pendingChoices.size() << " choices" << std::endl;
    return true;
}

// ============================================================
// 候補を pendingChoices に詰める（升級と報酬で共通）。詰めた数を返す
// ============================================================
int LevelUpSystem::FillChoices(LevelComponent& lv)
{
    // 候補の母集団を作る（重み付き）
    struct Entry { ItemID id; float weight; };
    std::vector<Entry> pool;
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        // 定義が取れないものは除く（登録漏れの保険）
        if (!ItemDatabase::GetCommon(id)) continue;
        pool.push_back({ id, 1.0f });
    }
    // 能力値（生命・魔力・速さ・跳躍…）も同じ池に混ぜる。種類が多いので 1 枚ずつの重みは下げる
    // （全部 1 だと候補の半分近くが能力値になる。2026-09-29 用户「基础数值的东西有点太多了」）
    for (ItemID id : ItemDatabase::GetLevelUpOnlyIDs())
        pool.push_back({ id, statWeight });

    // 重みに比例して 1 枚ずつ引く（引いた物は池から外す = 同じ物は並ばない）
    const int want = (choiceCount < 1) ? 1 : choiceCount;
    lv.pendingChoices.clear();
    while ((int)lv.pendingChoices.size() < want && !pool.empty())
    {
        float total = 0.0f;
        for (const Entry& e : pool) total += (std::max)(e.weight, 0.0f);
        if (total <= 0.0f) break;

        float r = (float)rand() / ((float)RAND_MAX + 1.0f) * total;
        size_t pick = pool.size() - 1;
        for (size_t i = 0; i < pool.size(); ++i)
        {
            r -= (std::max)(pool[i].weight, 0.0f);
            if (r < 0.0f) { pick = i; break; }
        }
        lv.pendingChoices.push_back(pool[pick].id);
        pool.erase(pool.begin() + (ptrdiff_t)pick);
    }
    return (int)lv.pendingChoices.size();
}

// ============================================================
// 選択の確定
// ============================================================
bool LevelUpSystem::Choose(Registry& reg, Entity player, ItemID picked)
{
    if (!reg.IsValid(player)) return false;
    if (!reg.Has<LevelComponent>(player))     return false;
    if (!reg.Has<SpellbookComponent>(player)) return false;

    auto& lv = reg.Get<LevelComponent>(player);
    auto& book = reg.Get<SpellbookComponent>(player);

    // 候補に無いものは受け付けない
    // UI 以外から呼ばれた時に、抽選を無視して習得できてしまうのを防ぐ
    bool found = false;
    for (ItemID id : lv.pendingChoices)
    {
        if (id == picked) { found = true; break; }
    }
    if (!found) return false;

    // ---- 能力値: その場で上限を上げる。呪文書には入れない ----
    if (const StatItemDef* stat = ItemDatabase::GetStat(picked))
    {
        ApplyStat(reg, player, *stat);
        lv.ClearChoices();
        std::cout << "[LevelUp] stat: " << stat->common.name << std::endl;
        return true;
    }

    // 既に持っていれば数が増える。強化ではない。
    book.Learn(picked, 1);
    lv.ClearChoices();

    const ItemCommon* c = ItemDatabase::GetCommon(picked);
    std::cout << "[LevelUp] learned: " << (c ? c->name : "?") << std::endl;

    return true;
}

// ============================================================
// 能力値の適用
// 上限と今の値を同じだけ足す。取った瞬間に増えたのが HUD で見えるように。
// 今の値は上限で丸める（満タンでも溢れない）
// ============================================================
void LevelUpSystem::ApplyStat(Registry& reg, Entity player, const StatItemDef& stat)
{
    switch (stat.kind)
    {
    case StatKind::MaxHealth:
        if (reg.Has<HealthComponent>(player))
        {
            auto& hp = reg.Get<HealthComponent>(player);
            hp.max += stat.amount;
            hp.current = (std::min)(hp.current + stat.amount, hp.max);
        }
        break;

    case StatKind::MaxMana:
        if (reg.Has<ManaComponent>(player))
        {
            auto& mp = reg.Get<ManaComponent>(player);
            mp.max += stat.amount;
            mp.current = (std::min)(mp.current + stat.amount, mp.max);
        }
        break;

    // 速さは今の値に掛ける（percent）か足す。PlayerControlSystem が毎フレーム読むのですぐ効く
    case StatKind::MoveSpeed:
        if (reg.Has<PlayerStatsComponent>(player))
        {
            auto& st = reg.Get<PlayerStatsComponent>(player);
            st.moveSpeed = stat.percent ? st.moveSpeed * (1.0f + stat.amount) : st.moveSpeed + stat.amount;
        }
        break;

    case StatKind::JumpPower:
        if (reg.Has<PlayerStatsComponent>(player))
        {
            auto& st = reg.Get<PlayerStatsComponent>(player);
            st.jumpPower = stat.percent ? st.jumpPower * (1.0f + stat.amount) : st.jumpPower + stat.amount;
        }
        break;

    // 魔力の回復（毎秒）。ManaSystem が毎フレーム読む
    case StatKind::ManaRegen:
        if (reg.Has<ManaComponent>(player))
        {
            auto& mp = reg.Get<ManaComponent>(player);
            mp.regen = stat.percent ? mp.regen * (1.0f + stat.amount) : mp.regen + stat.amount;
        }
        break;

    // 空中で跳べる回数。次に空中で跳ぶ時から効く
    case StatKind::JumpCount:
        if (reg.Has<PlayerStatsComponent>(player))
            reg.Get<PlayerStatsComponent>(player).extraJumps += (int)(stat.amount + 0.5f);
        break;
    }
}

bool LevelUpSystem::IsAnyoneChoosing(Registry& reg)
{
    bool choosing = false;
    reg.CreateView<LevelComponent, PlayerTag>()
        .Each([&](Entity, LevelComponent& lv, PlayerTag&)
            {
                if (lv.IsChoosing()) choosing = true;
            });
    return choosing;
}