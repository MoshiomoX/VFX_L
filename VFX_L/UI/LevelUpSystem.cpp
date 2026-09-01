// ============================================================
// LevelUpSystem.cpp
// ============================================================
#include "UI/LevelUpSystem.h"
#include "ECS/Registry.h"
#include "Player/LevelComponent.h"
#include "Component/SpellbookComponent.h"
#include "Player/PlayerTag.h"
#include "Item/ItemDatabase.h"
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

    // 候補の母集団を作る
    std::vector<ItemID> pool;
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        // 定義が取れないものは除く（登録漏れの保険）
        if (!ItemDatabase::GetCommon(id)) continue;
        pool.push_back(id);
    }

    if (pool.empty())
    {
        // 候補が1つも無いなら、レベルだけ上げて先へ進める。
        // ここで止まると経験値が溜まり続けて動かなくなる。
        lv.ConsumeLevelUp();
        ++m_TotalLevelUps;
        std::cout << "[LevelUp] no candidate available" << std::endl;
        return;
    }

    // 前から n 個を取り出す形にしたいので、シャッフルする。
    // 池が小さいうちは毎回似た組み合わせになるが、
    // 魔法が増えれば自然に散る。
    for (size_t i = pool.size() - 1; i > 0; --i)
    {
        const size_t j = (size_t)(rand() % (int)(i + 1));
        std::swap(pool[i], pool[j]);
    }

    const int n = (std::min)((int)pool.size(), (choiceCount < 1) ? 1 : choiceCount);

    lv.pendingChoices.clear();
    for (int i = 0; i < n; ++i)
        lv.pendingChoices.push_back(pool[i]);

    // 候補を出した時点でレベルを上げる。
    // 選び終わってから上げる方式にすると、
    // 選択中に経験値が入った場合の扱いが面倒になる。
    lv.ConsumeLevelUp();
    ++m_TotalLevelUps;

    std::cout << "[LevelUp] level " << lv.level
        << " : " << n << " choices" << std::endl;
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

    // 既に持っていれば数が増える。強化ではない。
    book.Learn(picked, 1);
    lv.ClearChoices();

    const ItemCommon* c = ItemDatabase::GetCommon(picked);
    std::cout << "[LevelUp] learned: " << (c ? c->name : "?") << std::endl;

    return true;
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