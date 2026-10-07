// ============================================================
// BossBrain.cpp
// ============================================================
#include "Enemy/BossBrain.h"
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
    default:                  return "none";
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
