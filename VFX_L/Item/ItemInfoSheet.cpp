// ============================================================
// ItemInfoSheet.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する。
// 説明カードの種類ごとの中身（飛び方の文・能力値の行・ルーンの修飾の行）。
// 2026-10-09 に ItemInfo.cpp（640 行）から分けた。名前・見出し・誘発の行・Describe は ItemInfo.cpp
// ============================================================
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
        case SpellParam::AreaDamage:
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
        case AreaParam::Length:
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
        case SpellParam::Lifetime:        return L"飛行時間";
        case SpellParam::ProjectileCount: return L"弾数";
        case SpellParam::SpreadAngle:     return L"拡散角度";
        case SpellParam::CastCount:       return L"連射";
        case SpellParam::CastDelay:       return L"連射の間隔";
        case SpellParam::CastInterval:    return L"発動間隔";
        case SpellParam::ManaCost:        return L"消費MP";
        case SpellParam::AreaDamage:      return L"範囲の威力";
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
        case AreaParam::Length:        return L"射程";
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
        case AreaParam::Radius:
        case AreaParam::Length:       return L"m";
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
}

namespace ItemInfoSheet
{
    using ItemInfo::Line;
    using ItemInfo::Sheet;

    // ---- 種類ごとの中身 ----
    // v = 修飾後、b = 元の値（修飾が無ければ同じ物を渡す）
    // 説明の文は短く（2026-10-09 ユーザー：「魔法の説明が長すぎる。プレイヤーは読まない」）。
    // 文（traits）は「何が起きるか」だけを一言で、数字は下の能力値の行へ出す。細かい例外（エリート・ボスは半分など）は書かない。
    // 半径・太さ・拡散角度のような細かい数字は、ルーンで変わった時だけ行を出す

    void FillProjectile(Sheet& s, const SpellStats& v, const SpellStats& b)
    {
        // 飛び方と命中時の範囲はプロファイルから。エディタで変えれば説明も変わる
        const ProjectileProfile& pp = ProjectileProfileDB::At(b.profile);
        // 隕石・投げ上げ（毒）は途中で敵に当たらない（弾そのものの威力は入らず、着弾の範囲だけ）
        const bool drop = pp.mode == Swarm::MotionMode::Drop || pp.mode == Swarm::MotionMode::Lob;

        // 命中・着弾で出る範囲（威力 0 の物は命中の見た目だけ = ArcSpark など。説明には出さない）
        const AreaProfile* ap = nullptr;
        if (!pp.hitArea.empty())
        {
            const int ai = AreaProfileDB::IndexOf(pp.hitArea);
            if (ai > 0 && AreaProfileDB::At(ai).damage > 0.0f) ap = &AreaProfileDB::At(ai);
        }
        const bool blast = ap && ap->kind == AreaProfile::Kind::OneShot;
        // 凍らせる一撃の範囲（アイススパイクの氷のトゲ。2026-10-10）は「爆発」と書かない
        const bool ice = blast && ap->freeze > 0.0f;

        // ---- 文：飛び方。足元に落ちる物は範囲と 1 文にまとめる ----
        if (b.triggerLine)   // 線で撃つ上級魔法（アイススパイク）：弾は飛ばない
            s.traits.push_back(ice ? L"足元から連鎖した場所の奥まで、氷のトゲが走る" : L"足元から連鎖した場所の奥まで、範囲が走る");
        else if (drop)
        {
            const std::wstring what = !ap ? L"に落ちる" : ice ? L"から前へ氷が突き出る" : blast ? L"で爆発する" : L"に範囲を残す";
            s.traits.push_back(kAtFeet + what);
        }
        else
        {
            switch (pp.mode)
            {
            case Swarm::MotionMode::Straight:  s.traits.push_back(L"まっすぐ飛ぶ"); break;
            case Swarm::MotionMode::CurveOnce: s.traits.push_back(L"近くの敵へ弧を描いて飛ぶ"); break;
            case Swarm::MotionMode::Track:     s.traits.push_back(L"敵を追い続ける"); break;
            default: break;
            }
            if (ap) s.traits.push_back(blast ? L"当たると爆発する" : L"当たった所に範囲を残す");
        }
        // 減速（毒の池）。15 段階に丸めた実際の値を出す（AreaProfile::Flags）
        if (ap && ap->slow > 0.0f)
        {
            const int pct = (int)std::lround(std::lround(std::clamp(ap->slow, 0.0f, 1.0f) * 15.0f) / 15.0f * 100.0f);
            wchar_t buf[64];
            swprintf_s(buf, L"中の敵を %d%% 遅くする", pct);
            s.traits.push_back(buf);
        }
        // 凍結（アイスランスの弾 2026-10-08 / アイススパイクの範囲 2026-10-10）。エリート・ボスは半分、解けた直後は凍らない、は書かない
        const float freeze = (std::max)(pp.freezeTime, ap ? ap->freeze : 0.0f);
        if (freeze > 0.0f)
        {
            wchar_t buf[64];
            swprintf_s(buf, L"当たった敵を %ls 秒凍らせる", Num(freeze).c_str());
            s.traits.push_back(buf);
        }

        // ---- 能力値の行 ----
        if (!drop)
            s.stats.push_back(StatLine(L"威力", v.damage, b.damage, L"", +1));
        float areaRadius = 0.0f, baseAreaRadius = 0.0f;
        if (ap)
        {
            // 当たり半径が profile より大きい（拡大鏡）と、GPU は命中の範囲も同じ倍率で広げる
            // （SwarmSpawnProjCS の sizeScale → SwarmSpawnAreaFromDef）
            areaRadius = ap->radius * ((pp.radius > 0.0f) ? v.radius / pp.radius : 1.0f);
            baseAreaRadius = ap->radius * ((pp.radius > 0.0f) ? b.radius / pp.radius : 1.0f);
            // 範囲の威力は魔法威力の分だけ上がる（GPU が弾ごとの areaDamageMul を掛ける）
            const float dmg = ap->damage * v.areaDamageMul, baseDmg = ap->damage * b.areaDamageMul;
            if (blast)
                s.stats.push_back(StatLine(ice ? L"威力" : L"爆発の威力", dmg, baseDmg, L"", +1));
            else
            {
                // 威力は「1 回分 / 間隔」で 1 行に
                const std::wstring per = L" / " + Num(ap->tickInterval) + L"秒";
                s.stats.push_back(StatLine(L"範囲の威力", dmg, baseDmg, per.c_str(), +1));
                s.stats.push_back(StatLine(L"範囲の持続", ap->duration, ap->duration, L"秒", +1));
            }
        }
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
        // 隕石は弾が当たらないので、当たり判定の行は出さない（大きさは範囲の半径に表れる）
        if (!drop && Changed(v.radius, b.radius)) s.stats.push_back(StatLine(L"当たり判定", v.radius, b.radius, L"m", +1));
        if (ap && Changed(areaRadius, baseAreaRadius))
            s.stats.push_back(StatLine((blast && !ice) ? L"爆発の半径" : L"範囲の半径", areaRadius, baseAreaRadius, L"m", +1));
        if (Changed(v.lifetime, b.lifetime)) s.stats.push_back(StatLine(L"飛行時間", v.lifetime, b.lifetime, L"秒", +1));
    }

    void FillArea(Sheet& s, const AreaStats& v, const AreaStats& b)
    {
        // 光線（AreaProfile::Kind::Beam）: 手から貫くカプセル。範囲の代わりに太さと射程
        const bool beam = v.profile > 0 && AreaProfileDB::At(v.profile).IsBeam();
        if (!beam)
            s.traits.push_back(v.spawnAtTarget ? L"敵の足元に出る" : L"自分の周りに出る");
        else if (AreaProfileDB::At(v.profile).chargeTime <= 0.0f && v.duration < 0.5f)
            s.traits.push_back(L"一番近い敵へ一瞬で届く");   // アークボルト（溜め 0 の稲妻。2026-10-15）
        if (v.profile > 0 && AreaProfileDB::At(v.profile).push > 0.0f)
            s.traits.push_back(L"前方の敵を吹き飛ばす");   // 突風（エリートは半分、ボスは動かない）

        // 威力は「1 回分 / 間隔」で 1 行に（間隔がルーンで変わった時は間隔の行を別に出す）
        const std::wstring per = L" / " + Num(v.tickInterval) + L"秒";
        s.stats.push_back(StatLine(L"威力", v.damagePerTick, b.damagePerTick, per.c_str(), +1));
        s.stats.push_back(StatLine(L"持続", v.duration, b.duration, L"秒", +1));
        s.stats.push_back(StatLine(L"発動間隔", v.castInterval, b.castInterval, L"秒", -1));
        s.stats.push_back(StatLine(L"消費MP", v.manaCost, b.manaCost, L"", -1));
        if (Changed(v.tickInterval, b.tickInterval))
            s.stats.push_back(StatLine(L"ダメージ間隔", v.tickInterval, b.tickInterval, L"秒", -1));
        // 光線：射程は常に出す（拡大鏡で伸びる）、太さは変わった時だけ
        if (beam)
            s.stats.push_back(StatLine(L"射程", v.length, b.length, L"m", +1));
        if (beam && Changed(v.radius, b.radius))
            s.stats.push_back(StatLine(L"太さ", v.radius * 2.0f, b.radius * 2.0f, L"m", +1));
        if (!beam && Changed(v.radius, b.radius))
            s.stats.push_back(StatLine(L"範囲", v.radius, b.radius, L"m", +1));
    }

    void FillFunction(Sheet& s, const FunctionItemDef& def)
    {
        for (const auto& m : def.spellModifiers)
            if (m.param != SpellParam::SpreadAngle)   // 拡散角度は細かい数字なので出さない
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
        l.label = L"サイズ";
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
        case StatKind::JumpPower: l.label = L"ジャンプ力"; break;
        case StatKind::ManaRegen: l.label = L"MP回復"; break;
        case StatKind::JumpCount: l.label = L"ジャンプ回数"; break;
        case StatKind::SpellPower: l.label = L"魔法の威力"; break;
        case StatKind::Shield:    l.label = L"最大シールド"; break;
        }
        l.value = def.percent ? L"+" + Num(def.amount * 100.0f) + L"%" : L"+" + Num(def.amount);
        l.trend = +1;
        s.stats.push_back(l);
    }

    // 召喚物（水晶玉）。数値は定義から
    void FillSummon(Sheet& s, const SummonItemDef& def)
    {
        s.stats.push_back(StatLine(L"光球の数", (float)def.maxOrbs, (float)def.maxOrbs, L"個", 0));
        s.stats.push_back(StatLine(L"出る間隔", def.orbInterval, def.orbInterval, L"秒", 0));
        s.stats.push_back(StatLine(L"光球の持続", def.orbLife, def.orbLife, L"秒", 0));
        s.stats.push_back(StatLine(L"貯めた魔法の消費MP", def.manaMul, def.manaMul, L"倍", 0));
    }

    // 貯蔵されている攻撃魔法の説明に足す行（集約と同じ判定）
    void AddStoredTrait(Sheet& s, const BackpackComponent& bp, int itemIndex)
    {
        const int unit = BackpackLogic::StoredBy(bp, itemIndex);
        if (unit < 0) return;
        s.traits.insert(s.traits.begin(), ItemInfo::DisplayName(bp.items[unit].id) + L"に貯蔵中（光球が撃つ）");
    }
}
