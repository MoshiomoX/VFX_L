// ============================================================
// BossBrain.cpp
// ============================================================
#include "Enemy/Boss/BossBrain.h"
#include "imgui.h"

const char* BossMoveName(BossMove m)
{
    switch (m)
    {
    case BossMove::Slam:      return "slam";
    case BossMove::RockRain:  return "rockrain";
    case BossMove::Shockwave: return "shockwave";
    case BossMove::Charge:    return "charge";
    case BossMove::Summon:    return "summon";
    case BossMove::SandSpear: return "sandspear";
    case BossMove::Burrow:    return "burrow";
    case BossMove::Quicksand: return "quicksand";
    case BossMove::Sting:     return "sting";
    case BossMove::ClawSweep: return "clawsweep";
    case BossMove::ChainPull: return "chainpull";
    case BossMove::Pounce:    return "pounce";
    case BossMove::ClawCombo: return "clawcombo";
    case BossMove::Howl:      return "howl";
    default:                  return "none";
    }
}

const char* BossTypeName(BossType t)
{
    switch (t)
    {
    case BossType::StoneGiant:   return "stone giant";
    case BossType::SandGuardian: return "sand guardian";
    case BossType::Scorpion:     return "scorpion";
    case BossType::Stag:         return "thunder stag";
    case BossType::CryptKnight:  return "crypt knight";
    case BossType::Werewolf:     return "werewolf";
    default:                     return "?";
    }
}

const wchar_t* BossDisplayName(BossType t)
{
    switch (t)
    {
    case BossType::StoneGiant:   return L"岩の巨人";
    case BossType::SandGuardian: return L"砂の守護者";
    case BossType::Scorpion:     return L"大サソリ";
    case BossType::Stag:         return L"雷鳴の雄鹿";
    case BossType::CryptKnight:  return L"墓守の騎士";
    case BossType::Werewolf:     return L"月下の狼人";
    default:                     return L"?";
    }
}

BossBrain::BossBrain()
{
    // 重撃：どの距離でも（足元に置く技）、近いほど多め
    rules[(int)BossMove::Slam] = { true, 5.0f, 3.0f, 2.0f, 1.0f, 0.0f, 35.0f };
    // 投石雨：遠いほど多め（広く散らして走らせる）
    rules[(int)BossMove::RockRain] = { true, 8.0f, 1.0f, 2.0f, 3.0f, 0.0f, 35.0f };
    // 衝撃波：近〜中（帯は 28m で消えるので、それより遠いと届かない）
    rules[(int)BossMove::Shockwave] = { true, 8.0f, 3.0f, 2.0f, 0.5f, 0.0f, 26.0f };
    // 突進：中〜遠（目の前で溜めても意味が無いので 5m 未満は出さない）
    rules[(int)BossMove::Charge] = { true, 9.0f, 0.0f, 2.0f, 3.0f, 5.0f, 30.0f };
    // 召喚：たまに（冷却長め）。遠いと少し多め（距離を詰められない間に数で押す）
    rules[(int)BossMove::Summon] = { true, 14.0f, 1.0f, 1.0f, 2.0f, 0.0f, 35.0f };
    // ---- 砂の守護者だけの技（既定では切っておき、BossAttacks::Configure が点ける）----
    // 砂の槍：中〜遠（線は 22m。目の前では横へ避ける間が無い）
    rules[(int)BossMove::SandSpear] = { false, 7.0f, 0.5f, 3.0f, 2.0f, 4.0f, 24.0f };
    // 潜砂奇襲：遠いほど多め（離れたプレイヤーへ距離を詰める技）
    rules[(int)BossMove::Burrow] = { false, 12.0f, 0.3f, 1.5f, 3.0f, 6.0f, 35.0f };
    // 流沙：近〜中（渦は半径 10m。それより遠いと届かない）。冷却は渦の長さより長く
    rules[(int)BossMove::Quicksand] = { false, 15.0f, 3.0f, 2.0f, 0.0f, 0.0f, 10.0f };
    // ---- 大サソリだけの技（2026-10-26）----
    // 毒針：中くらいの距離（突っ込みは 8m。目の前では避ける間が無い、遠いと届かない）
    rules[(int)BossMove::Sting] = { false, 6.0f, 1.0f, 3.0f, 0.5f, 3.0f, 14.0f };
    // 鋏の薙ぎ払い：近く（扇は 5.5m）
    rules[(int)BossMove::ClawSweep] = { false, 5.0f, 3.0f, 1.0f, 0.0f, 0.0f, 7.0f };
    // ---- 墓守の騎士だけの技（2026-10-26）----
    // 双鎖：中〜遠（鎖は 14m。目の前では引き寄せる意味が無い）
    rules[(int)BossMove::ChainPull] = { false, 7.0f, 0.5f, 3.0f, 2.0f, 3.0f, 14.0f };
    // ---- 月下の狼人だけの技（2026-10-26）----
    // 飛びかかり：中〜遠（5m 未満は跳ぶ意味が無い、20m まで届く）
    rules[(int)BossMove::Pounce] = { false, 7.0f, 0.5f, 3.0f, 3.0f, 5.0f, 20.0f };
    // 爪の三連：近〜中（突っ込みは 6m × 3）
    rules[(int)BossMove::ClawCombo] = { false, 5.0f, 3.0f, 2.0f, 0.5f, 0.0f, 12.0f };
    // 遠吠え：たまに。遠いと多め
    rules[(int)BossMove::Howl] = { false, 16.0f, 1.0f, 1.0f, 2.0f, 0.0f, 35.0f };
    Reset();
}

void BossBrain::Reset()
{
    for (float& c : m_Cooldown) c = 0.0f;
    m_Last = BossMove::None;
}

void BossBrain::Tick(float dt)
{
    for (float& c : m_Cooldown)
        if (c > 0.0f) c -= dt;
}

BossMove BossBrain::Choose(float dist, std::mt19937& rng) const
{
    float w[(int)BossMove::Count] = {};
    float total = 0.0f;
    int eligible = 0;
    for (int pass = 0; pass < 2 && total <= 0.0f; ++pass)
    {
        // 1 回目：直前の技は外す。それで何も無ければ 2 回目は直前の技も入れる
        for (int i = 0; i < (int)BossMove::Count; ++i)
        {
            const Rule& r = rules[i];
            w[i] = 0.0f;
            if (!r.enabled || m_Cooldown[i] > 0.0f || dist < r.minDist || dist > r.maxDist) continue;
            if (pass == 0 && (BossMove)i == m_Last) continue;
            w[i] = (dist < nearDist) ? r.wNear : (dist > farDist) ? r.wFar : r.wMid;
            total += w[i];
            if (w[i] > 0.0f) ++eligible;
        }
    }
    if (total <= 0.0f || eligible == 0) return BossMove::None;

    std::uniform_real_distribution<float> u(0.0f, total);
    float pick = u(rng);
    for (int i = 0; i < (int)BossMove::Count; ++i)
    {
        if (w[i] <= 0.0f) continue;
        if (pick < w[i]) return (BossMove)i;
        pick -= w[i];
    }
    for (int i = (int)BossMove::Count - 1; i >= 0; --i)
        if (w[i] > 0.0f) return (BossMove)i;
    return BossMove::None;
}

void BossBrain::OnStarted(BossMove m, bool enraged)
{
    if (m == BossMove::None) return;
    m_Cooldown[(int)m] = rules[(int)m].cooldown * (enraged ? enragedCooldownMul : 1.0f);
    m_Last = m;
}

void BossBrain::DrawImGui()
{
    ImGui::DragFloat("Near < (m)##brain", &nearDist, 0.5f, 0.0f, 100.0f);
    ImGui::DragFloat("Far > (m)##brain", &farDist, 0.5f, 0.0f, 100.0f);
    ImGui::DragFloat("Enraged cooldown x##brain", &enragedCooldownMul, 0.01f, 0.1f, 2.0f);
    for (int i = 0; i < (int)BossMove::Count; ++i)
    {
        Rule& r = rules[i];
        ImGui::PushID(i);
        ImGui::Checkbox(BossMoveName((BossMove)i), &r.enabled);
        ImGui::SameLine(110.0f);
        ImGui::Text("cd %.1f s  (last: %s)", m_Cooldown[i] > 0.0f ? m_Cooldown[i] : 0.0f, BossMoveName(m_Last));
        ImGui::DragFloat("Cooldown (s)", &r.cooldown, 0.1f, 0.0f, 60.0f);
        ImGui::DragFloat3("Weight near / mid / far", &r.wNear, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat2("Min / max dist (m)", &r.minDist, 0.5f, 0.0f, 200.0f);
        ImGui::PopID();
    }
}
