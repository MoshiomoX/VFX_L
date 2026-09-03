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
// ============================================================
void BackpackAggregateSystem::Rebuild(Registry& reg, Entity e)
{
    auto& bp = reg.Get<BackpackComponent>(e);
    auto& wand = reg.Get<WandComponent>(e);

    m_Log.clear();

    // ---- 1) 影響格を持つブロックを集めて、ワールド座標のマスに展開する ----
    //     回転を反映した後の位置（画布内のみ）を持っておく
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

    // ---- 2) 攻撃ブロックごとに出力を組む ----
    wand.spells.clear();
    wand.areas.clear();

    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        const auto& item = bp.items[i];
        if (!ItemDatabase::IsAttackType(item.id)) continue;

        const ItemCommon* c = ItemDatabase::GetCommon(item.id);
        if (!c) continue;

        // このブロックの占位格（回転反映後）
        auto occupy = BackpackLogic::RotateShape(c->occupyCells, item.rotation);

        // ---- このブロックに影響を与えているブロックを探す ----
        // 影響格と占位格が1マスでも重なれば成立。
        //   同じブロックから複数マス重なっても1回だけ数える（set にする理由）
        std::unordered_set<size_t> influencers;

        for (const auto& src : sources)
        {
            if (src.itemIndex == i) continue;   // 自分自身は数えない

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

        // ---- ログ ----
        AggregateLog log;
        log.sourceName = c->name;
        log.row = item.row;
        log.col = item.col;

        // ---- 種類ごとに基礎値へ修飾を重ねる ----
        if (auto* pdef = ItemDatabase::GetProjectile(item.id))
        {
            SpellStats stats = pdef->baseStats;

            for (size_t srcIdx : influencers)
            {
                const auto& srcItem = bp.items[srcIdx];
                auto* fdef = ItemDatabase::GetFunction(srcItem.id);
                if (!fdef) continue;   // 機能型でないものは影響格を持っていても無視

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