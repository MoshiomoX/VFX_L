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
#include "Player/PlayerStatsComponent.h"
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
    // 能力アップ「魔法威力」。ルーンの修飾の後に掛ける（ItemInfo::DescribePlaced と同じ順）。
    // 札を取ったら LevelUpSystem::ApplyStat が dirty を立てるので、ここで掛け直される
    const float power = reg.Has<PlayerStatsComponent>(e) ? reg.Get<PlayerStatsComponent>(e).spellPower : 1.0f;

    m_Log.clear();

    // ---- 攻撃ブロックごとに出力を組む ----
    wand.spells.clear();
    wand.areas.clear();
    wand.orbs.clear();

    // ---- 召喚物（水晶玉。2026-10-06）を先に並べる。貯蔵された魔法は storeUnit でここを指す ----
    // 召喚物はルーンの影響を受けない（影響マスが届いていても無視）。光球の色は貯蔵した魔法の色を後で混ぜる
    std::vector<int> orbIndexOf(bp.items.size(), -1);
    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        const SummonItemDef* sdef = ItemDatabase::GetSummon(bp.items[i].id);
        if (!sdef) continue;
        OrbUnitStats u;
        u.id = sdef->common.id;
        u.itemIndex = (int)i;
        u.maxOrbs = sdef->maxOrbs;
        u.orbInterval = sdef->orbInterval;
        u.orbLife = sdef->orbLife;
        u.orbitRadius = sdef->orbitRadius;
        u.orbitHeight = sdef->orbitHeight;
        u.orbitSpeed = sdef->orbitSpeed;
        u.vfxFile = sdef->vfxFile;
        u.color = { 0, 0, 0 };
        orbIndexOf[i] = (int)wand.orbs.size();
        wand.orbs.push_back(u);
    }
    // 貯蔵: 魔法 i を貯蔵している召喚物の orbs の添字（無ければ -1）。消費 MP の倍率と混色もここで
    auto storeInto = [&](size_t i, float& manaCost, const DirectX::SimpleMath::Vector4& color) -> int
        {
            const int src = BackpackLogic::StoredBy(bp, (int)i);
            if (src < 0 || orbIndexOf[src] < 0) return -1;
            const SummonItemDef* sdef = ItemDatabase::GetSummon(bp.items[src].id);
            OrbUnitStats& u = wand.orbs[orbIndexOf[src]];
            manaCost *= sdef ? sdef->manaMul : 1.0f;
            u.color += DirectX::SimpleMath::Vector3(color.x, color.y, color.z);
            ++u.storedCount;
            return orbIndexOf[src];
        };

    // items の index → wand.spells の添字（-1 = 飛行物でない / 撃たない）。誘発の bit を後で引く
    std::vector<int> spellIndexOf(bp.items.size(), -1);
    std::vector<int> areaIndexOf(bp.items.size(), -1);    // 同じく wand.areas の添字（光線の誘発用）

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
                if (!fdef) continue;   // 機能型でないものは影響マスを持っていても無視

                for (const auto& mod : fdef->spellModifiers)
                    ItemDatabase::ApplyModifier(stats, mod);

                log.influencedBy.push_back(fdef->common.name);
            }
            ItemInfo::ApplySpellPower(stats, power);

            // 貯蔵（水晶玉）: 杖からは撃たず光球が撃つ。上級魔法でも光球が直接撃つ（triggered にしない）
            stats.storeUnit = storeInto(i, stats.manaCost, c->color);
            if (stats.storeUnit >= 0) log.influencedBy.push_back("(stored in crystal ball)");

            // 上級魔法: 前提の基本魔法が全種類届いていなければ撃たない（バックパックでは暗く出る）
            if (!pdef->common.triggeredBy.empty() && stats.storeUnit < 0)
            {
                if (!BackpackLogic::IsTriggerReady(bp, (int)i))
                {
                    log.influencedBy.push_back("(inactive: needs triggers)");
                    m_Log.push_back(std::move(log));
                    continue;
                }
                stats.triggered = true;
            }

            spellIndexOf[i] = (int)wand.spells.size();
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
            ItemInfo::ApplySpellPower(stats, power);

            // 貯蔵（水晶玉）: 光球の位置から撃つ。上級魔法（光線）でも光球が直接撃つ
            stats.storeUnit = storeInto(i, stats.manaCost, c->color);
            if (stats.storeUnit >= 0) log.influencedBy.push_back("(stored in crystal ball)");

            // 上級魔法（光線）: 前提が揃っていなければ出さない（バックパックでは暗く出る）
            if (!adef->common.triggeredBy.empty() && stats.storeUnit < 0)
            {
                if (!BackpackLogic::IsTriggerReady(bp, (int)i))
                {
                    log.influencedBy.push_back("(inactive: needs triggers)");
                    m_Log.push_back(std::move(log));
                    continue;
                }
                stats.triggered = true;
            }

            areaIndexOf[i] = (int)wand.areas.size();
            wand.areas.push_back(stats);
        }

        m_Log.push_back(std::move(log));
    }

    // ---- 誘発: 有効な上級魔法 k に影響マスを届かせている基本魔法に bit k を立てる ----
    // 1 つの基本魔法が複数の上級魔法に届いていれば全部立つ（どれも自分のクールダウンで撃つ）
    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        const int k = spellIndexOf[i];
        if (k < 0 || k >= 32 || !wand.spells[k].triggered) continue;

        for (int drv : BackpackLogic::GetTriggerDrivers(bp, (int)i))
        {
            const int d = spellIndexOf[drv];
            if (d >= 0 && !wand.spells[d].triggered)
                wand.spells[d].triggerMask |= 1u << k;
        }
    }
    // 同じく上級の範囲魔法（光線）j は bit (16 + j)
    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        const int j = areaIndexOf[i];
        if (j < 0 || j >= 16 || !wand.areas[j].triggered) continue;

        for (int drv : BackpackLogic::GetTriggerDrivers(bp, (int)i))
        {
            const int d = spellIndexOf[drv];
            if (d >= 0 && !wand.spells[d].triggered)
                wand.spells[d].triggerMask |= 1u << (16 + j);
        }
    }

    // 光球の色 = 貯蔵した魔法の色の平均（何も貯蔵していなければ召喚物自身の色）
    for (OrbUnitStats& u : wand.orbs)
    {
        if (u.storedCount > 0) u.color /= (float)u.storedCount;
        else if (const ItemCommon* c = ItemDatabase::GetCommon(u.id)) u.color = { c->color.x, c->color.y, c->color.z };
    }

    bp.dirty = false;
    ++m_RebuildCount;

    std::cout << "[Backpack] rebuilt: " << wand.spells.size() << " spells, "
        << wand.areas.size() << " areas" << std::endl;
}
