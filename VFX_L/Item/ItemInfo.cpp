// ============================================================
// ItemInfo.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する。
//   UI の字体は Yuji Syuku（Res::Fnt::JP。2026-09-28 から）で、× ° → や全角の記号も出るが、
//   文はそれ以前（NotoSansJP の頃）の書き方のまま：掛け算は x、角度は「度」と書く
// ============================================================
#include "Item/ItemInfo.h"
#include "Item/ItemInfoSheet.h"
#include "Item/ItemDatabase.h"
#include "Item/BackpackLogic.h"
#include "Component/BackpackComponent.h"
#include "Swarm/ProjectileProfile.h"
#include "Swarm/AreaProfile.h"

namespace
{
    using ItemInfo::Line;
    using ItemInfo::Sheet;

    // 内部名（ASCII）を wstring に
    std::wstring Widen(const char* s)
    {
        std::wstring out;
        if (!s) return out;
        while (*s) out.push_back((wchar_t)(unsigned char)*s++);
        return out;
    }

    std::wstring Join(const std::vector<std::wstring>& items)
    {
        std::wstring out;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0) out += L"、";
            out += items[i];
        }
        return out;
    }

    Sheet Header(ItemID id)
    {
        Sheet s;
        s.id = id;
        s.title = ItemInfo::DisplayName(id);
        s.category = ItemInfo::CategoryLabel(ItemDatabase::GetCategory(id));
        if (const ItemCommon* c = ItemDatabase::GetCommon(id))
        {
            if (!c->triggeredBy.empty()) s.category = L"上級魔法";   // 前提の基本魔法が要る物（隕石・光線）
            if (!c->outletCells.empty()) s.category = L"出口魔法";   // 2026-10-31
            if (const FrameItemDef* fd = ItemDatabase::GetFrame(id); fd && fd->HasEffect()) s.category = L"効果の枠";   // 2026-10-27
            s.color = c->color;
            s.description = c->description ? c->description : L"";
        }
        return s;
    }
}

using namespace ItemInfoSheet;   // Fill… / kAtFeet（種類ごとの中身は ItemInfoSheet.cpp）

namespace ItemInfo
{
    std::wstring DisplayName(ItemID id)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) return L"?";
        if (c->displayName && *c->displayName) return c->displayName;
        return Widen(c->name);
    }

    const wchar_t* CategoryLabel(ItemCategory c)
    {
        switch (c)
        {
        case ItemCategory::Projectile: return L"攻撃魔法";
        case ItemCategory::Function:   return L"ルーン";
        case ItemCategory::Area:       return L"範囲魔法";
        case ItemCategory::Frame:      return L"拡張枠";
        case ItemCategory::Stat:       return L"能力アップ";
        case ItemCategory::Summon:     return L"召喚物";
        default:                       return L"";
        }
    }

    // 弾そのもの（威力・速さ・判定・寿命）は投射物プロファイルが基礎値。
    // アイテム側の値は使わない。名前が引けなければ 0 番（組み込みの直進）
    SpellStats BaseSpellStats(const ProjectileItemDef& def)
    {
        SpellStats stats = def.baseStats;
        stats.profile = ProjectileProfileDB::IndexOf(def.profile);
        const ProjectileProfile& pp = ProjectileProfileDB::At(stats.profile);
        stats.damage = pp.damage;
        stats.speed = pp.speed;
        stats.radius = pp.radius;
        stats.lifetime = pp.lifetime;
        return stats;
    }

    // エディタのプロファイルがあれば、形・時間・威力はそちらが基礎値
    AreaStats BaseAreaStats(const AreaItemDef& def)
    {
        AreaStats stats = def.baseStats;
        stats.profile = AreaProfileDB::IndexOf(def.profile);
        if (stats.profile > 0)
        {
            const AreaProfile& ap = AreaProfileDB::At(stats.profile);
            stats.radius = ap.radius;
            stats.duration = ap.duration;
            stats.tickInterval = ap.tickInterval;
            stats.damagePerTick = ap.damage;
            stats.length = ap.IsBeam() ? ap.length : 0.0f;
        }
        return stats;
    }

    void ApplySpellPower(SpellStats& s, float power)
    {
        s.damage *= power;
        s.areaDamageMul *= power;
    }

    void ApplySpellPower(AreaStats& a, float power)
    {
        a.damagePerTick *= power;
    }

    // 上級魔法の前提（「ファイアボール・ストーンショット」）
    static std::wstring TriggerNames(const ItemCommon& c)
    {
        std::vector<std::wstring> names;
        for (ItemID need : c.triggeredBy) names.push_back(DisplayName(need));
        return Join(names);
    }

    // 上級魔法: 「敵の足元…」を「連鎖した場所…」に差し替える（前提の弾が消えた場所に落ちる）
    static void FixTriggeredFlight(Sheet& s, const ProjectileItemDef& d)
    {
        if (d.common.outletText && d.common.outletText[0]) s.traits.insert(s.traits.begin(), d.common.outletText);   // 出口魔法（2026-10-31）
        if (d.common.triggeredBy.empty()) return;
        const std::wstring feet = kAtFeet;
        for (auto& t : s.traits)
            if (t.compare(0, feet.size(), feet) == 0) t = L"連鎖した場所" + t.substr(feet.size());
    }

    // バックパックに置いた物の「誘発」の行（集約と同じ判定）。上級魔法なら目覚めているか / 足りない前提、
    // 基本魔法なら自分が呼び起こしている上級魔法
    static void AddTriggerTraits(Sheet& s, const BackpackComponent& bp, int itemIndex, const ItemCommon& c, int shotsPerCast = 1)
    {
        // 出口（2026-10-31）：自分の出口の先 / 自分を出口の先に持つ魔法
        if (!c.outletCells.empty())
        {
            std::vector<std::wstring> names;
            for (int t : BackpackLogic::GetOutletTargets(bp, itemIndex))
                names.push_back(DisplayName(bp.items[t].id) + (shotsPerCast > 1 ? L" x" + std::to_wstring(shotsPerCast) : L""));
            s.traits.insert(s.traits.begin(), names.empty() ? std::wstring(L"出口の先に魔法が無い") : L"出口の先: " + Join(names));
        }
        const std::vector<int> outletDrivers = BackpackLogic::GetOutletDrivers(bp, itemIndex);
        if (!outletDrivers.empty())
        {
            std::vector<std::wstring> names;
            for (int d : outletDrivers) names.push_back(DisplayName(bp.items[d].id));
            s.traits.insert(s.traits.begin(), Join(names) + L" の弾 1 発につき 1 回発動");
        }
        if (!c.triggeredBy.empty())
        {
            if (BackpackLogic::IsTriggerReady(bp, itemIndex))
                s.traits.insert(s.traits.begin(), L"発動中");
            else
            {
                const std::vector<int> drivers = BackpackLogic::GetTriggerDrivers(bp, itemIndex);
                std::vector<std::wstring> missing;
                for (ItemID need : c.triggeredBy)
                {
                    bool found = false;
                    for (int k : drivers) if (bp.items[k].id == need) { found = true; break; }
                    if (!found) missing.push_back(DisplayName(need));
                }
                s.traits.insert(s.traits.begin(), L"未発動: " + Join(missing) + L" が隣に無い");
            }
            return;
        }

        std::vector<std::wstring> wakes;
        for (int j = 0; j < (int)bp.items.size(); ++j)
        {
            if (j == itemIndex) continue;
            const auto drv = BackpackLogic::GetTriggerDrivers(bp, j);
            if (std::find(drv.begin(), drv.end(), itemIndex) != drv.end()
                && BackpackLogic::IsTriggerReady(bp, j))
                wakes.push_back(DisplayName(bp.items[j].id));
        }
        if (!wakes.empty())
            s.traits.insert(s.traits.begin(), L"連鎖: " + Join(wakes));
    }

    Sheet Describe(ItemID id)
    {
        Sheet s = Header(id);
        if (auto* d = ItemDatabase::GetProjectile(id))
        {
            const SpellStats b = BaseSpellStats(*d);
            FillProjectile(s, b, b);
            FixTriggeredFlight(s, *d);
            if (!d->common.triggeredBy.empty())
                s.traits.insert(s.traits.begin(),
                    L"隣に必要: " + TriggerNames(d->common));
        }
        else if (auto* d = ItemDatabase::GetArea(id))
        {
            const AreaStats b = BaseAreaStats(*d);
            FillArea(s, b, b);
            if (!d->common.triggeredBy.empty())
                s.traits.insert(s.traits.begin(),
                    L"隣に必要: " + TriggerNames(d->common));
        }
        else if (auto* d = ItemDatabase::GetFunction(id))
            FillFunction(s, *d);
        else if (auto* d = ItemDatabase::GetFrame(id))
            FillFrame(s, *d);
        else if (auto* d = ItemDatabase::GetStat(id))
            FillStat(s, *d);
        else if (auto* d = ItemDatabase::GetSummon(id))
            FillSummon(s, *d);
        return s;
    }

    // 置いてある魔法の値：隣のルーン → 効果の枠 → 魔法威力 → 貯蔵の MP（集約と同じ順）。by = 強化の名前
    static SpellStats PlacedSpell(const BackpackComponent& bp, int itemIndex, const ProjectileItemDef& d, float spellPower, std::vector<std::wstring>* by)
    {
        SpellStats v = BaseSpellStats(d);
        for (int i : BackpackLogic::GetInfluencers(bp, itemIndex))
        {
            const FunctionItemDef* f = ItemDatabase::GetFunction(bp.items[i].id);
            if (!f) continue;
            for (const auto& m : f->spellModifiers) ItemDatabase::ApplyModifier(v, m);
            if (by) by->push_back(DisplayName(f->common.id));
        }
        for (int fi : BackpackLogic::GetEffectFrames(bp, itemIndex))
        {
            const FrameItemDef* fr = ItemDatabase::GetFrame(bp.frames[fi].id);
            if (!fr) continue;
            for (const auto& m : fr->spellModifiers) ItemDatabase::ApplyModifier(v, m);
            if (by) by->push_back(DisplayName(fr->common.id));
        }
        ApplySpellPower(v, spellPower);
        if (const int unit = BackpackLogic::StoredBy(bp, itemIndex); unit >= 0)
            if (const SummonItemDef* sd = ItemDatabase::GetSummon(bp.items[unit].id)) v.manaCost *= sd->manaMul;
        return v;
    }
    static AreaStats PlacedArea(const BackpackComponent& bp, int itemIndex, const AreaItemDef& d, float spellPower, std::vector<std::wstring>* by)
    {
        AreaStats v = BaseAreaStats(d);
        for (int i : BackpackLogic::GetInfluencers(bp, itemIndex))
        {
            const FunctionItemDef* f = ItemDatabase::GetFunction(bp.items[i].id);
            if (!f) continue;
            for (const auto& m : f->areaModifiers) ItemDatabase::ApplyModifier(v, m);
            if (by) by->push_back(DisplayName(f->common.id));
        }
        for (int fi : BackpackLogic::GetEffectFrames(bp, itemIndex))
        {
            const FrameItemDef* fr = ItemDatabase::GetFrame(bp.frames[fi].id);
            if (!fr) continue;
            for (const auto& m : fr->areaModifiers) ItemDatabase::ApplyModifier(v, m);
            if (by) by->push_back(DisplayName(fr->common.id));
        }
        ApplySpellPower(v, spellPower);
        if (const int unit = BackpackLogic::StoredBy(bp, itemIndex); unit >= 0)
            if (const SummonItemDef* sd = ItemDatabase::GetSummon(bp.items[unit].id)) v.manaCost *= sd->manaMul;
        return v;
    }

    // 出口の線（集約と同じ計算）
    static std::vector<BackpackLogic::OutletLine> PlacedLines(const BackpackComponent& bp, float spellPower)
    {
        std::vector<BackpackLogic::OutletNode> nodes(bp.items.size());
        for (int i = 0; i < (int)bp.items.size(); ++i)
        {
            if (auto* d = ItemDatabase::GetProjectile(bp.items[i].id))
            {
                const SpellStats v = PlacedSpell(bp, i, *d, spellPower, nullptr);
                nodes[i] = { v.castInterval, BaseSpellStats(*d).castInterval, v.manaCost, v.projectileCount, v.castCount };
            }
            else if (auto* d = ItemDatabase::GetArea(bp.items[i].id))
            {
                const AreaStats v = PlacedArea(bp, i, *d, spellPower, nullptr);
                nodes[i] = { v.castInterval, BaseAreaStats(*d).castInterval, v.manaCost, 1, 1 };
            }
        }
        return BackpackLogic::SolveOutletLines(BackpackLogic::GetOutletEdges(bp), nodes);
    }

    Sheet DescribePlaced(const BackpackComponent& bp, int itemIndex, float spellPower)
    {
        if (itemIndex < 0 || itemIndex >= (int)bp.items.size()) return Sheet{};
        const ItemID id = bp.items[itemIndex].id;
        Sheet s = Header(id);

        // ---- 攻撃魔法：隣のルーンの修飾を集約と同じ順で掛ける ----
        std::vector<std::wstring> by;

        if (auto* d = ItemDatabase::GetProjectile(id))
        {
            const SpellStats b = BaseSpellStats(*d);
            SpellStats v = PlacedSpell(bp, itemIndex, *d, spellPower, &by);
            const int shotsPerCast = (std::max)(v.projectileCount, 1) * (std::max)(v.castCount, 1);
            // 出口の線：起点は線全体の間隔と MP、下流は上流の分裂の分だけ威力が下がる（集約と同じ）
            const BackpackLogic::OutletLine line = PlacedLines(bp, spellPower)[itemIndex];
            if (!BackpackLogic::GetOutletDrivers(bp, itemIndex).empty())
            {
                v.damage *= line.powerMul;
                v.areaDamageMul *= line.powerMul;
            }
            else if (!BackpackLogic::GetOutletTargets(bp, itemIndex).empty())
            {
                v.castInterval = line.interval;
                v.manaCost = line.mana;
            }
            FillProjectile(s, v, b);
            FixTriggeredFlight(s, *d);
            if (!by.empty()) s.footer = L"強化: " + Join(by);

            AddTriggerTraits(s, bp, itemIndex, d->common, shotsPerCast);   // 誘発（集約と同じ判定）
            AddStoredTrait(s, bp, itemIndex);
            return s;
        }
        if (auto* d = ItemDatabase::GetArea(id))
        {
            const AreaStats b = BaseAreaStats(*d);
            AreaStats v = PlacedArea(bp, itemIndex, *d, spellPower, &by);
            if (!BackpackLogic::GetOutletDrivers(bp, itemIndex).empty())
                v.damagePerTick *= PlacedLines(bp, spellPower)[itemIndex].powerMul;
            FillArea(s, v, b);
            if (!by.empty()) s.footer = L"強化: " + Join(by);
            AddTriggerTraits(s, bp, itemIndex, d->common);   // 光線（上級魔法）の誘発
            AddStoredTrait(s, bp, itemIndex);
            return s;
        }

        // ---- 召喚物（水晶玉）：何を貯蔵しているか ----
        if (auto* d = ItemDatabase::GetSummon(id))
        {
            FillSummon(s, *d);
            std::vector<std::wstring> stored;
            for (int j = 0; j < (int)bp.items.size(); ++j)
                if (j != itemIndex && BackpackLogic::StoredBy(bp, j) == itemIndex)
                    stored.push_back(DisplayName(bp.items[j].id));
            s.footer = stored.empty() ? L"貯蔵中: なし (光るマスに魔法を置く)" : L"貯蔵中: " + Join(stored);
            return s;
        }

        // ---- ルーン：どの魔法に効いているか ----
        if (auto* d = ItemDatabase::GetFunction(id))
        {
            FillFunction(s, *d);
            std::vector<std::wstring> targets;
            for (int j = 0; j < (int)bp.items.size(); ++j)
            {
                if (j == itemIndex || !ItemDatabase::IsAttackType(bp.items[j].id)) continue;
                const auto others = BackpackLogic::GetInfluencers(bp, j);
                if (std::find(others.begin(), others.end(), itemIndex) != others.end())
                    targets.push_back(DisplayName(bp.items[j].id));
            }
            s.footer = targets.empty() ? L"強化中: なし (光るマスに魔法を置く)" : L"強化中: " + Join(targets);
            return s;
        }

        return Describe(id);
    }
}
