// ============================================================
// LifestealHeal.cpp
// ============================================================
#include "Player/LifestealHeal.h"
#include "Component/HealthComponent.h"
#include "imgui.h"

LifestealHeal::Result LifestealHeal::Update(float dt, float stolen, HealthComponent& hp, bool alive)
{
    Result r;
    if (!alive)
    {
        Reset();
        return r;
    }
    const float cap = (std::max)(0.0f, capPerSec * hp.max);
    m_Bank = (std::min)(m_Bank + (std::max)(0.0f, stolen) * ratio, cap * bankSec);
    const float room = (std::max)(0.0f, hp.max - hp.current);
    const float heal = (std::min)({ m_Bank, cap * dt, room });
    if (heal > 0.0f)
    {
        hp.current += heal;
        m_Bank -= heal;
        m_Total += heal;
        m_Shown += heal;
        r.healed = heal;
    }
    if (room <= 0.0f) m_Bank = 0.0f;   // 満タンの間は溜めない（満タンで殴り続けた分が後から戻らないように）

    m_ShowTimer -= dt;
    if (m_ShowTimer <= 0.0f && m_Shown >= 0.5f)
    {
        m_ShowTimer = showInterval;
        r.showAmount = m_Shown;
        r.playFx = true;
        m_Shown = 0.0f;
    }
    return r;
}

void LifestealHeal::DrawImGui()
{
    if (!ImGui::TreeNode("Lifesteal (rune)")) return;
    ImGui::Text("healed total %.1f   banked %.1f", m_Total, m_Bank);
    ImGui::DragFloat("Ratio", &ratio, 0.001f, 0.0f, 1.0f);
    ImGui::DragFloat("Cap per sec (x max HP)", &capPerSec, 0.001f, 0.0f, 1.0f);
    ImGui::DragFloat("Bank (sec)", &bankSec, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Show interval", &showInterval, 0.01f, 0.05f, 3.0f);
    ImGui::TreePop();
}
