// ============================================================
// ItemInfo.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する。
//   文の書き方：掛け算は x、角度は「度」と書く
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
            if (!c->outletCells.empty()) s.category = L"出口魔法";
            if (const FrameItemDef* fd = ItemDatabase::GetFrame(id); fd && fd->HasEffect()) s.category = L"効果の枠";
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

    // 出口魔法の説明の行
    static void AddOutletText(Sheet& s, const ItemCommon& c)
    {
        if (c.outletText && c.outletText[0]) s.traits.insert(s.traits.begin(), c.outletText);
    }

    // バックパックに置いた物の出口の行（集約と同じ判定）
    static void AddOutletTraits(Sheet& s, const BackpackComponent& bp, int itemIndex, const ItemCommon& c, int shotsPerCast = 1)
    {
        // 出口：自分の出口の先 / 自分を出口の先に持つ魔法
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
    }

    Sheet Describe(ItemID id)
    {
        Sheet s = Header(id);
        if (auto* d = ItemDatabase::GetProjectile(id))
        {
            const SpellStats b = BaseSpellStats(*d);
            FillProjectile(s, b, b);
            AddOutletText(s, d->common);
        }
        else if (auto* d = ItemDatabase::GetArea(id))
        {
            const AreaStats b = BaseAreaStats(*d);
            FillArea(s, b, b);
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
            AddOutletText(s, d->common);
            if (!by.empty()) s.footer = L"強化: " + Join(by);

            AddOutletTraits(s, bp, itemIndex, d->common, shotsPerCast);
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
            AddOutletTraits(s, bp, itemIndex, d->common);
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
