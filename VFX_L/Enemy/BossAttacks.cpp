// ============================================================
// BossAttacks.cpp
// ============================================================
#include "Enemy/BossAttacks.h"
#include "World/GridWorld.h"
#include "imgui.h"
#include <algorithm>

using DirectX::SimpleMath::Vector3;

void BossAttacks::Reset()
{
    m_Rings.clear();
    m_Timer = firstDelay;
    m_Pending = 0;
    m_PendingTimer = 0.0f;
    m_WasAlive = false;
    m_DebugVolley = false;
}

int BossAttacks::Update(float dt, bool bossAlive, float bossHpRatio, const Vector3& bossPos,
    const Vector3& player, float damageMul, const GridWorld& grid, std::vector<Blast>& outBlasts)
{
    // Boss が居なくなったら（倒した・まだ呼んでいない）輪も消す
    if (!bossAlive)
    {
        if (m_WasAlive || !m_Rings.empty()) Reset();
        return 0;
    }
    if (!m_WasAlive)
    {
        m_WasAlive = true;
        m_Timer = firstDelay;
    }

    // ---- 育てて、縁に届いた物は爆発 ----
    for (Ring& r : m_Rings) r.age += dt;
    for (auto it = m_Rings.begin(); it != m_Rings.end();)
    {
        if (it->age >= warnTime)
        {
            outBlasts.push_back({ it->center, it->radius, damage * damageMul });
            ++blasts;
            it = m_Rings.erase(it);
        }
        else
            ++it;
    }

    // ---- 技の拍子 ----
    const Vector3 toPlayer = player - bossPos;
    const bool inRange = Vector3(toPlayer.x, 0.0f, toPlayer.z).Length() <= range;
    m_Timer -= dt;
    const bool start = m_DebugVolley || (enabled && inRange && m_Timer <= 0.0f && m_Pending == 0);
    if (start)
    {
        m_DebugVolley = false;
        m_Pending = (std::max)(1, count);
        m_PendingTimer = 0.0f;
        m_Timer = (bossHpRatio < enrageHpRatio) ? intervalEnraged : interval;
        ++volleys;
    }
    else if (!inRange && m_Timer < 0.0f)
        m_Timer = 0.0f;   // 離れている間は溜めない（近付いた瞬間に 1 回だけ）

    // ---- 輪を置く（置いた瞬間のプレイヤーの足元）----
    int placed = 0;
    if (m_Pending > 0)
    {
        m_PendingTimer -= dt;
        while (m_Pending > 0 && m_PendingTimer <= 0.0f)
        {
            Ring r;
            r.center = Vector3(player.x, grid.SampleHeight(player.x, player.z), player.z);
            r.radius = radius;
            r.age = 0.0f;
            m_Rings.push_back(r);
            --m_Pending;
            ++placed;
            ++ringsPlaced;
            m_PendingTimer += spacing;
        }
    }
    return placed;
}

void BossAttacks::DrawImGui()
{
    ImGui::TextColored(ImVec4(0.85f, 0.4f, 1.0f, 1), "Boss Attacks (ground slam)");
    ImGui::Text("rings %d  pending %d  next %.1f s   volleys %u rings %u blasts %u",
        (int)m_Rings.size(), m_Pending, m_Timer, volleys, ringsPlaced, blasts);
    ImGui::Checkbox("Slam Enabled", &enabled);
    ImGui::DragFloat("Slam First Delay (s)", &firstDelay, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat("Slam Interval (s)", &interval, 0.1f, 0.5f, 60.0f);
    ImGui::DragFloat("Slam Interval Enraged (s)", &intervalEnraged, 0.1f, 0.5f, 60.0f);
    ImGui::SliderFloat("Enrage HP Ratio", &enrageHpRatio, 0.0f, 1.0f);
    ImGui::DragInt("Slam Rings", &count, 1, 1, 12);
    ImGui::DragFloat("Slam Spacing (s)", &spacing, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Slam Warn Time (s)", &warnTime, 0.05f, 0.1f, 5.0f);
    ImGui::DragFloat("Slam Radius (m)", &radius, 0.05f, 0.5f, 10.0f);
    ImGui::DragFloat("Slam Damage (base)", &damage, 0.5f, 0.0f, 500.0f);
    ImGui::DragFloat("Slam Range (m)", &range, 0.5f, 1.0f, 200.0f);
    if (ImGui::Button("Slam Now")) QueueVolley();
}
