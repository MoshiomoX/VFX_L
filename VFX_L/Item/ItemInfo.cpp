// ============================================================
// ItemInfo.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する。
//   フォント（NotoSansJP）に無い字は使わない：全角英数・記号（！？（）など）、
//   × ° → は出ない。掛け算は x、角度は「度」と書く
// ============================================================
#include "Item/ItemInfo.h"
#include "Item/ItemDatabase.h"
#include "Item/BackpackLogic.h"
#include "Component/BackpackComponent.h"
#include "Swarm/ProjectileProfile.h"
#include "Swarm/AreaProfile.h"
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace
{
    using ItemInfo::Line;
    using ItemInfo::Sheet;

    // 数値の文字列。整数ならそのまま、そうでなければ小数 2 桁まで（末尾の 0 は落とす）
    std::wstring Num(float v)
    {
        wchar_t buf[32];
        const float r = std::round(v);
        if (std::fabs(v - r) < 0.005f)
        {
            swprintf_s(buf, L"%d", (int)r);
            return buf;
        }
        swprintf_s(buf, L"%.2f", v);
        std::wstring s = buf;
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
        return s;
    }

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

    // ---- パラメータごとの表示 ----
    // Better: 大きいほど良い +1 / 小さいほど良い -1 / 好みの問題 0
    int Better(SpellParam p)
    {
        switch (p)
        {
        case SpellParam::Damage:
        case SpellParam::Speed:
        case SpellParam::Radius:
        case SpellParam::Lifetime:
        case SpellParam::ProjectileCount:
        case SpellParam::CastCount:
            return +1;
        case SpellParam::CastInterval:
        case SpellParam::ManaCost:
            return -1;
        default:
            return 0;   // 拡散・連射の間隔
        }
    }

    int Better(AreaParam p)
    {
        switch (p)
        {
        case AreaParam::Radius:
        case AreaParam::Duration:
        case AreaParam::DamagePerTick:
            return +1;
        case AreaParam::TickInterval:
        case AreaParam::CastInterval:
        case AreaParam::ManaCost:
            return -1;
        default:
            return 0;
        }
    }

    const wchar_t* Label(SpellParam p)
    {
        switch (p)
        {
        case SpellParam::Damage:          return L"威力";
        case SpellParam::Speed:           return L"弾速";
        case SpellParam::Radius:          return L"当たり判定";
        case SpellParam::Lifetime:        return L"飛ぶ時間";
        case SpellParam::ProjectileCount: return L"弾数";
        case SpellParam::SpreadAngle:     return L"拡散";
        case SpellParam::CastCount:       return L"連射";
        case SpellParam::CastDelay:       return L"連射の間隔";
        case SpellParam::CastInterval:    return L"発動間隔";
        case SpellParam::ManaCost:        return L"消費MP";
        }
        return L"";
    }

    const wchar_t* Label(AreaParam p)
    {
        switch (p)
        {
        case AreaParam::Radius:        return L"範囲";
        case AreaParam::Duration:      return L"持続";
        case AreaParam::TickInterval:  return L"ダメージ間隔";
        case AreaParam::DamagePerTick: return L"威力";
        case AreaParam::CastInterval:  return L"発動間隔";
        case AreaParam::ManaCost:      return L"消費MP";
        }
        return L"";
    }

    const wchar_t* Unit(SpellParam p)
    {
        switch (p)
        {
        case SpellParam::Speed:        return L"m/秒";
        case SpellParam::Radius:       return L"m";
        case SpellParam::Lifetime:
        case SpellParam::CastDelay:
        case SpellParam::CastInterval: return L"秒";
        case SpellParam::SpreadAngle:  return L"度";
        case SpellParam::CastCount:    return L"回";
        default:                       return L"";
        }
    }

    const wchar_t* Unit(AreaParam p)
    {
        switch (p)
        {
        case AreaParam::Radius:       return L"m";
        case AreaParam::Duration:
        case AreaParam::TickInterval:
        case AreaParam::CastInterval: return L"秒";
        default:                      return L"";
        }
    }

    // 修飾 1 件を「+1」「x0.6」「=5」の形の行に（ルーンの説明用）
    template <class P>
    Line ModifierLine(P param, ModifyOp op, float value, const wchar_t* prefix = L"")
    {
        Line l;
        l.label = std::wstring(prefix) + Label(param);
        int dir = 0;   // 値が増えるなら +1、減るなら -1
        switch (op)
        {
        case ModifyOp::Add:
            l.value = (value >= 0.0f ? L"+" : L"") + Num(value) + Unit(param);
            dir = (value > 0.0f) - (value < 0.0f);
            break;
        case ModifyOp::Multiply:
            l.value = L"x" + Num(value);
            dir = (value > 1.0f) - (value < 1.0f);
            break;
        case ModifyOp::Set:
            l.value = L"=" + Num(value) + Unit(param);
            break;
        }
        l.trend = dir * Better(param);
        return l;
    }

    // 能力値の 1 行。value が base と違えば元の値も付け、良くなったか悪くなったかを入れる
    Line StatLine(const wchar_t* label, float value, float base, const wchar_t* unit, int better)
    {
        Line l;
        l.label = label;
        l.value = Num(value) + unit;
        if (std::fabs(value - base) > 1e-4f)
        {
            l.baseValue = Num(base) + unit;
            l.trend = ((value > base) ? 1 : -1) * better;
        }
        return l;
    }

    bool Changed(float a, float b) { return std::fabs(a - b) > 1e-4f; }

    // 範囲魔法が 1 つでも登録されているか（無いのにルーンの範囲向け修飾を並べると紛らわしい）
    bool HasAreaItems()
    {
        for (ItemID id : ItemDatabase::GetAllIDs())
            if (ItemDatabase::GetCategory(id) == ItemCategory::Area) return true;
        return false;
    }

    Sheet Header(ItemID id)
    {
        Sheet s;
        s.id = id;
        s.title = ItemInfo::DisplayName(id);
        s.category = ItemInfo::CategoryLabel(ItemDatabase::GetCategory(id));
        if (const ItemCommon* c = ItemDatabase::GetCommon(id))
        {
            s.color = c->color;
            s.description = c->description ? c->description : L"";
        }
        return s;
    }

    // ---- 種類ごとの中身 ----
    // v = 修飾後、b = 元の値（修飾が無ければ同じ物を渡す）
    void FillProjectile(Sheet& s, const SpellStats& v, const SpellStats& b)
    {
        // 飛び方と命中時の範囲はプロファイルから。編集器で変えれば説明も変わる
        const ProjectileProfile& pp = ProjectileProfileDB::At(b.profile);
        switch (pp.mode)
        {
        case Swarm::MotionMode::Straight:  s.traits.push_back(L"まっすぐ飛ぶ"); break;
        case Swarm::MotionMode::CurveOnce: s.traits.push_back(L"撃った時に一番近い敵を狙って曲がる"); break;
        case Swarm::MotionMode::Track:     s.traits.push_back(L"敵を追い続ける。倒したら次の敵へ"); break;
        case Swarm::MotionMode::Drop:      s.traits.push_back(L"一番近い敵の足元へ空から落ちてくる"); break;
        }
        // 隕石は落ちる途中で敵に当たらない（弾そのものの威力は入らず、着弾の範囲だけ）
        const bool drop = pp.mode == Swarm::MotionMode::Drop;
        if (!pp.hitArea.empty())
        {
            const int ai = AreaProfileDB::IndexOf(pp.hitArea);
            // 威力 0 の範囲は命中の見た目だけ（ArcSpark など）。説明には出さない
            if (ai > 0 && AreaProfileDB::At(ai).damage > 0.0f)
            {
                const AreaProfile& ap = AreaProfileDB::At(ai);
                // 当たり半径が profile より大きい（拡大鏡）と、GPU は命中の範囲も同じ倍率で広げる
                // （SwarmSpawnProjCS の sizeScale → SwarmSpawnAreaFromDef）
                const float areaScale = (pp.radius > 0.0f) ? v.radius / pp.radius : 1.0f;
                wchar_t buf[160];
                if (ap.kind == AreaProfile::Kind::OneShot)
                    swprintf_s(buf, drop ? L"着弾すると爆発する (威力 %ls、半径 %lsm)" : L"命中すると爆発する (威力 %ls、半径 %lsm)",
                        Num(ap.damage).c_str(), Num(ap.radius * areaScale).c_str());
                else
                    swprintf_s(buf, L"命中した所に範囲を残す (%ls 秒ごとに威力 %ls、%ls 秒間)",
                        Num(ap.tickInterval).c_str(), Num(ap.damage).c_str(), Num(ap.duration).c_str());
                s.traits.push_back(buf);
            }
        }

        if (!drop)
            s.stats.push_back(StatLine(L"威力", v.damage, b.damage, L"", +1));
        if (v.projectileCount != 1 || v.projectileCount != b.projectileCount)
            s.stats.push_back(StatLine(L"弾数", (float)v.projectileCount, (float)b.projectileCount, L"", +1));
        if (v.castCount != 1 || v.castCount != b.castCount)
            s.stats.push_back(StatLine(L"連射", (float)v.castCount, (float)b.castCount, L"回", +1));
        s.stats.push_back(StatLine(L"発動間隔", v.castInterval, b.castInterval, L"秒", -1));
        // 連射の分も毎回払う（WeaponSystem）ので、1 回の発動で減る量を出す
        s.stats.push_back(StatLine(L"消費MP", v.manaCost * (float)v.castCount,
            b.manaCost * (float)b.castCount, L"", -1));

        // 修飾で変わった時だけ出す（行が多いと読まれない）
        if (Changed(v.speed, b.speed))       s.stats.push_back(StatLine(L"弾速", v.speed, b.speed, L"m/秒", +1));
        // 隕石は弾が当たらないので、当たり判定の行は出さない（大きさは爆発の半径に表れる）
        if (!drop && Changed(v.radius, b.radius)) s.stats.push_back(StatLine(L"当たり判定", v.radius, b.radius, L"m", +1));
        if (Changed(v.lifetime, b.lifetime)) s.stats.push_back(StatLine(L"飛ぶ時間", v.lifetime, b.lifetime, L"秒", +1));
        if (v.projectileCount > 1)           s.stats.push_back(StatLine(L"拡散", v.spreadAngle, b.spreadAngle, L"度", 0));
    }

    void FillArea(Sheet& s, const AreaStats& v, const AreaStats& b)
    {
        s.traits.push_back(v.spawnAtTarget ? L"一番近い敵の足元に出る" : L"自分の周りに出る");
        s.stats.push_back(StatLine(L"威力", v.damagePerTick, b.damagePerTick, L"", +1));
        s.stats.push_back(StatLine(L"範囲", v.radius, b.radius, L"m", +1));
        s.stats.push_back(StatLine(L"持続", v.duration, b.duration, L"秒", +1));
        s.stats.push_back(StatLine(L"ダメージ間隔", v.tickInterval, b.tickInterval, L"秒", -1));
        s.stats.push_back(StatLine(L"発動間隔", v.castInterval, b.castInterval, L"秒", -1));
        s.stats.push_back(StatLine(L"消費MP", v.manaCost, b.manaCost, L"", -1));
    }

    void FillFunction(Sheet& s, const FunctionItemDef& def)
    {
        s.traits.push_back(L"薄く光るマスに置いた魔法を強化する");
        for (const auto& m : def.spellModifiers)
            s.stats.push_back(ModifierLine(m.param, m.op, m.value));
        if (HasAreaItems())
            for (const auto& m : def.areaModifiers)
                s.stats.push_back(ModifierLine(m.param, m.op, m.value, L"範囲魔法の"));
    }

    void FillFrame(Sheet& s, const ItemCommon& c)
    {
        int minR = 0, maxR = 0, minC = 0, maxC = 0;
        for (size_t i = 0; i < c.occupyCells.size(); ++i)
        {
            const auto& o = c.occupyCells[i];
            if (i == 0) { minR = maxR = o.row; minC = maxC = o.col; continue; }
            minR = (std::min)(minR, o.row); maxR = (std::max)(maxR, o.row);
            minC = (std::min)(minC, o.col); maxC = (std::max)(maxC, o.col);
        }
        Line l;
        l.label = L"広さ";
        l.value = Num((float)(maxC - minC + 1)) + L"x" + Num((float)(maxR - minR + 1))
            + L" (" + Num((float)c.occupyCells.size()) + L" マス)";
        s.stats.push_back(l);
    }

    void FillStat(Sheet& s, const StatItemDef& def)
    {
        Line l;
        switch (def.kind)
        {
        case StatKind::MaxHealth: l.label = L"最大HP"; break;
        case StatKind::MaxMana:   l.label = L"最大MP"; break;
        case StatKind::MoveSpeed: l.label = L"移動速度"; break;
        case StatKind::JumpPower: l.label = L"跳躍力"; break;
        case StatKind::ManaRegen: l.label = L"魔力回復"; break;
        case StatKind::JumpCount: l.label = L"跳躍回数"; break;
        }
        l.value = def.percent ? L"+" + Num(def.amount * 100.0f) + L"%" : L"+" + Num(def.amount);
        l.trend = +1;
        s.stats.push_back(l);
    }
}

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
        default:                       return L"";
        }
    }

    // 弾そのもの（威力・速さ・判定・寿命）は投射物プロファイルが基礎値。
    // 道具側の値は使わない。名前が引けなければ 0 番（組み込みの直進）
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

    // 編集器のプロファイルがあれば、形・時間・威力はそちらが基礎値
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
        }
        return stats;
    }

    Sheet Describe(ItemID id)
    {
        Sheet s = Header(id);
        if (auto* d = ItemDatabase::GetProjectile(id))
        {
            const SpellStats b = BaseSpellStats(*d);
            FillProjectile(s, b, b);
        }
        else if (auto* d = ItemDatabase::GetArea(id))
        {
            const AreaStats b = BaseAreaStats(*d);
            FillArea(s, b, b);
        }
        else if (auto* d = ItemDatabase::GetFunction(id))
            FillFunction(s, *d);
        else if (auto* d = ItemDatabase::GetFrame(id))
            FillFrame(s, d->common);
        else if (auto* d = ItemDatabase::GetStat(id))
            FillStat(s, *d);
        return s;
    }

    Sheet DescribePlaced(const BackpackComponent& bp, int itemIndex)
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
            FillProjectile(s, v, b);
            if (!by.empty()) s.footer = L"強化: " + Join(by);
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
            FillArea(s, v, b);
            if (!by.empty()) s.footer = L"強化: " + Join(by);
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
