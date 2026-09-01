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
#include "ECS/View.h"
#include <unordered_set>
#include <iostream>

void BackpackAggregateSystem::Update(Registry& reg)
{
    // dirty ??????????(??????????????)
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
// ?????
// ============================================================
void BackpackAggregateSystem::Rebuild(Registry& reg, Entity e)
{
    auto& bp = reg.Get<BackpackComponent>(e);
    auto& wand = reg.Get<WandComponent>(e);

    m_Log.clear();

    // ---- 1) ?????????????????????????? ----
    //     ????????(????)???????????
    struct InfluenceSource
    {
        size_t itemIndex;
        std::vector<std::pair<int, int>> cells;   // (row, col)
    };
    std::vector<InfluenceSource> sources;

    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        const auto& item = bp.items[i];
        const ItemCommon* c = ItemDatabase::GetCommon(item.id);
        if (!c || c->influenceCells.empty()) continue;

        InfluenceSource src;
        src.itemIndex = i;

        auto rotated = BackpackLogic::RotateShape(c->influenceCells, item.rotation);
        for (const auto& off : rotated)
        {
            int r = item.row + off.row;
            int cc = item.col + off.col;
            if (r < 0 || r >= BackpackComponent::GRID) continue;
            if (cc < 0 || cc >= BackpackComponent::GRID) continue;
            src.cells.push_back({ r, cc });
        }
        if (!src.cells.empty())
            sources.push_back(std::move(src));
    }

    // ---- 2) ??????????? ----
    wand.spells.clear();
    wand.areas.clear();

    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        const auto& item = bp.items[i];
        if (!ItemDatabase::IsAttackType(item.id)) continue;

        const ItemCommon* c = ItemDatabase::GetCommon(item.id);
        if (!c) continue;

        // ????????????????
        auto occupy = BackpackLogic::RotateShape(c->occupyCells, item.rotation);

        // ---- ???????????????????? ----
        // ???????????1???????
        //   ?????????????????????????????
        std::unordered_set<size_t> influencers;

        for (const auto& src : sources)
        {
            if (src.itemIndex == i) continue;   // ???????????

            bool touched = false;
            for (const auto& cell : src.cells)
            {
                for (const auto& off : occupy)
                {
                    if (cell.first == item.row + off.row &&
                        cell.second == item.col + off.col)
                    {
                        touched = true;
                        break;
                    }
                }
                if (touched) break;
            }
            if (touched)
                influencers.insert(src.itemIndex);
        }

        // ---- ??? ----
        AggregateLog log;
        log.sourceName = c->name;
        log.row = item.row;
        log.col = item.col;

        // ---- ???????????????????? ----
        if (auto* pdef = ItemDatabase::GetProjectile(item.id))
        {
            SpellStats stats = pdef->baseStats;

            for (size_t srcIdx : influencers)
            {
                const auto& srcItem = bp.items[srcIdx];
                auto* fdef = ItemDatabase::GetFunction(srcItem.id);
                if (!fdef) continue;   // ??????????????????

                for (const auto& mod : fdef->spellModifiers)
                    ItemDatabase::ApplyModifier(stats, mod);

                log.influencedBy.push_back(fdef->common.name);
            }

            wand.spells.push_back(stats);
        }
        else if (auto* adef = ItemDatabase::GetArea(item.id))
        {
            AreaStats stats = adef->baseStats;

            for (size_t srcIdx : influencers)
            {
                const auto& srcItem = bp.items[srcIdx];
                auto* fdef = ItemDatabase::GetFunction(srcItem.id);
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