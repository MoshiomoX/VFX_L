// ============================================================
// ItemInfo.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する。
//   フォント（NotoSansJP）に無い字は使わない：全角英数・記号（！？（）など）、
//   × ° → は出ない。掛け算は x、角度は「度」と書く
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
        if (d.common.triggeredBy.empty()) return;
        const std::wstring feet = kAtFeet;
        for (auto& t : s.traits)
            if (t.compare(0, feet.size(), feet) == 0) t = L"連鎖した場所" + t.substr(feet.size());
    }

    // バックパックに置いた物の「誘発」の行（集約と同じ判定）。上級魔法なら目覚めているか / 足りない前提、
    // 基本魔法なら自分が呼び起こしている上級魔法
    static void AddTriggerTraits(Sheet& s, const BackpackComponent& bp, int itemIndex, const ItemCommon& c)
    {
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
            FillFrame(s, d->common);
        else if (auto* d = ItemDatabase::GetStat(id))
            FillStat(s, *d);
        else if (auto* d = ItemDatabase::GetSummon(id))
            FillSummon(s, *d);
        return s;
    }

    Sheet DescribePlaced(const BackpackComponent& bp, int itemIndex, float spellPower)
    {
        if (itemIndex < 0 || itemIndex >= (int)bp.items.size()) return Sheet{};
        const ItemID id = bp.items[itemIndex].id;
        Sheet s = Header(id);

        // ---- 攻撃魔法：隣のルーンの修飾を集約と同じ順で掛ける ----
        const std::vector<int> infl = BackpackLogic::GetInfluencers(bp, itemIndex);
        std::vector<std::wstring> by;

        if (auto* d = ItemDatabase::GetProjectile(id))
        {
            const SpellStats b = BaseSpellStats(*d);
            SpellStats v = b;
            for (int i : infl)
            {
                const FunctionItemDef* f = ItemDatabase::GetFunction(bp.items[i].id);
                if (!f) continue;
                for (const auto& m : f->spellModifiers) ItemDatabase::ApplyModifier(v, m);
                by.push_back(DisplayName(f->common.id));
            }
            ApplySpellPower(v, spellPower);   // 集約と同じく修飾の後
            // 貯蔵（水晶玉）: 消費 MP の倍率（集約と同じ）
            if (const int unit = BackpackLogic::StoredBy(bp, itemIndex); unit >= 0)
                if (const SummonItemDef* sd = ItemDatabase::GetSummon(bp.items[unit].id)) v.manaCost *= sd->manaMul;
            FillProjectile(s, v, b);
            FixTriggeredFlight(s, *d);
            if (!by.empty()) s.footer = L"強化: " + Join(by);

            AddTriggerTraits(s, bp, itemIndex, d->common);   // 誘発（集約と同じ判定）
            AddStoredTrait(s, bp, itemIndex);
            return s;
        }
        if (auto* d = ItemDatabase::GetArea(id))
        {
            const AreaStats b = BaseAreaStats(*d);
            AreaStats v = b;
            for (int i : infl)
            {
                const FunctionItemDef* f = ItemDatabase::GetFunction(bp.items[i].id);
                if (!f) continue;
                for (const auto& m : f->areaModifiers) ItemDatabase::ApplyModifier(v, m);
                by.push_back(DisplayName(f->common.id));
            }
            ApplySpellPower(v, spellPower);
            if (const int unit = BackpackLogic::StoredBy(bp, itemIndex); unit >= 0)
                if (const SummonItemDef* sd = ItemDatabase::GetSummon(bp.items[unit].id)) v.manaCost *= sd->manaMul;
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
