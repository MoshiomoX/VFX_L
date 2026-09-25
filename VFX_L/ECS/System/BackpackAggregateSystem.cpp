// ============================================================
// BackpackAggregateSystem.cpp
// ============================================================
#include "ECS/System/BackpackAggregateSystem.h"
#include "ECS/Registry.h"
#include "Component/BackpackComponent.h"
#include "Item/BackpackLogic.h"
#include "Component/WandComponent.h"
#include "Component/AreaStats.h"
#include "Item/ItemDatabase.h"
#include "Item/ItemInfo.h"
#include "ECS/View.h"
#include <iostream>

void BackpackAggregateSystem::Update(Registry& reg)
{
    // dirty なものだけ集める（走査中に書き換えないよう、溜めてから処理する）
    std::vector<Entity> targets;
    reg.CreateView<BackpackComponent, WandComponent>()
        .Each([&](Entity e, BackpackComponent& bp, WandComponent&)
            {
                if (bp.dirty) targets.push_back(e);
            });

    for (Entity e : targets)
        Rebuild(reg, e);
}

void BackpackAggregateSystem::ForceRebuild(Registry& reg, Entity e)
{
    if (!reg.IsValid(e)) return;
    if (!reg.Has<BackpackComponent>(e) || !reg.Has<WandComponent>(e)) return;
    Rebuild(reg, e);
}

// ============================================================
// 再構築の本体
//   基礎値の組み立て（ItemInfo::Base*Stats）と影響の判定（BackpackLogic::GetInfluencers）は
//   UI の説明と共用。画面に出る数字と実際に撃つ数字を同じ式で出すため
// ============================================================
void BackpackAggregateSystem::Rebuild(Registry& reg, Entity e)
{
    auto& bp = reg.Get<BackpackComponent>(e);
    auto& wand = reg.Get<WandComponent>(e);

    m_Log.clear();

    // ---- 攻撃ブロックごとに出力を組む ----
    wand.spells.clear();
    wand.areas.clear();

    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        const auto& item = bp.items[i];
        if (!ItemDatabase::IsAttackType(item.id)) continue;

        const ItemCommon* c = ItemDatabase::GetCommon(item.id);
        if (!c) continue;

        // このブロックに影響を与えているブロック（items の順。同じ相手は 1 回だけ）
        const std::vector<int> influencers = BackpackLogic::GetInfluencers(bp, (int)i);

        // ---- ログ ----
        AggregateLog log;
        log.sourceName = c->name;
        log.row = item.row;
        log.col = item.col;

        // ---- 種類ごとに基礎値へ修飾を重ねる ----
        if (auto* pdef = ItemDatabase::GetProjectile(item.id))
        {
            SpellStats stats = ItemInfo::BaseSpellStats(*pdef);

            for (int srcIdx : influencers)
            {
                auto* fdef = ItemDatabase::GetFunction(bp.items[srcIdx].id);
                if (!fdef) continue;   // 機能型でないものは影響格を持っていても無視

                for (const auto& mod : fdef->spellModifiers)
                    ItemDatabase::ApplyModifier(stats, mod);

                log.influencedBy.push_back(fdef->common.name);
            }

            wand.spells.push_back(stats);
        }
        else if (auto* adef = ItemDatabase::GetArea(item.id))
        {
            AreaStats stats = ItemInfo::BaseAreaStats(*adef);

            for (int srcIdx : influencers)
            {
                auto* fdef = ItemDatabase::GetFunction(bp.items[srcIdx].id);
                if (!fdef) continue;

                for (const auto& mod : fdef->areaModifiers)
                    ItemDatabase::ApplyModifier(stats, mod);

                log.influencedBy.push_back(fdef->common.name);
            }

            wand.areas.push_back(stats);
        }

        m_Log.push_back(std::move(log));
    }

    bp.dirty = false;
    ++m_RebuildCount;

    std::cout << "[Backpack] rebuilt: " << wand.spells.size() << " spells, "
        << wand.areas.size() << " areas" << std::endl;
}
