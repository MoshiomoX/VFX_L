// ============================================================
// BackpackAggregateSystem.cpp
// ============================================================
#include "BackpackAggregateSystem.h"
#include "Registry.h"
#include "BackpackComponent.h"
#include "BackpackLogic.h"
#include "WandComponent.h"
#include "AreaStats.h"
#include "ItemDatabase.h"
#include "View.h"
#include <unordered_set>
#include <iostream>

void BackpackAggregateSystem::Update(Registry& reg)
{
    // dirty なものだけ再計算する（戦闘中は毎フレーム何もしない）
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
// 集約の本体
// ============================================================
void BackpackAggregateSystem::Rebuild(Registry& reg, Entity e)
{
    auto& bp = reg.Get<BackpackComponent>(e);
    auto& wand = reg.Get<WandComponent>(e);

    m_Log.clear();

    // ---- 1) 各ブロックの「影響を及ぼすマス集合」を先に求めておく ----
    //     影響格はワールド（グリッド）座標に変換して保持する
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

    // ---- 2) 出力源リストを作り直す ----
    wand.spells.clear();
    wand.areas.clear();

    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        const auto& item = bp.items[i];
        if (!ItemDatabase::IsAttackType(item.id)) continue;

        const ItemCommon* c = ItemDatabase::GetCommon(item.id);
        if (!c) continue;

        // このブロックが占めているマス集合
        auto occupy = BackpackLogic::RotateShape(c->occupyCells, item.rotation);

        // ---- どのブロックから影響を受けているか調べる ----
        // ※同じ相手からの影響は1回だけ数える。
        //   接触面が多い異形ブロックが自動的に有利になるのを防ぐため。
        std::unordered_set<size_t> influencers;

        for (const auto& src : sources)
        {
            if (src.itemIndex == i) continue;   // 自分自身の影響格は無視

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

        // ---- ログ用 ----
        AggregateLog log;
        log.sourceName = c->name;
        log.row = item.row;
        log.col = item.col;

        // ---- 大類型ごとに基礎値を取り、修飾を適用する ----
        if (auto* pdef = ItemDatabase::GetProjectile(item.id))
        {
            SpellStats stats = pdef->baseStats;

            for (size_t srcIdx : influencers)
            {
                const auto& srcItem = bp.items[srcIdx];
                auto* fdef = ItemDatabase::GetFunction(srcItem.id);
                if (!fdef) continue;   // 攻撃ブロック同士の影響は今は効果なし

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