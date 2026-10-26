// ============================================================
// EliteAttacksPanel.cpp
// Enemies パネルの「Elites (stage)」：面の固定エリートを出す・型毎の調整値・詞缀の調整値・数え
// ============================================================
#include "Enemy/Elite/EliteAttacks.h"
#include "imgui.h"

namespace
{
    const char* TypeLabel(EliteType t)
    {
        static const char* kNames[] = { "Axe warlord", "Forest archer", "Dust rider", "Flame mage", "Shadow swordsman", "Cursed librarian" };
        return ((int)t < (int)EliteType::Count) ? kNames[(int)t] : "?";
    }
}

void EliteAttacks::DrawImGui(int& spawn, uint32_t& spawnAffixes)
{
    spawn = -1;
    if (!ImGui::TreeNode("Elites (stage, 2026-10-25)")) return;

    ImGui::Text("A = %s   B = %s   alive %d   orbs %d   arrows %d   strikes %d",
        TypeLabel(m_TypeA), TypeLabel(m_TypeB), (int)m_Elites.size(), (int)m_Orbs.size(), (int)m_Arrows.size(), (int)m_Strikes.size());
    ImGui::Text("moves %d  teleports %d  shield breaks %d  enrages %d", m_Stats.moves, m_Stats.teleports, m_Stats.shieldBreaks, m_Stats.enrages);

    static bool affix[kEliteAffixCount] = {};
    const char* affixLabels[kEliteAffixCount] = { "Swift", "Iron", "Thunder", "Berserk" };
    for (int b = 0; b < kEliteAffixCount; ++b)
    {
        if (b > 0) ImGui::SameLine();
        ImGui::Checkbox(affixLabels[b], &affix[b]);
    }
    uint32_t mask = 0;
    for (int b = 0; b < kEliteAffixCount; ++b) if (affix[b]) mask |= 1u << b;
    if (ImGui::Button("Spawn Elite A")) { spawn = 0; spawnAffixes = mask; }
    ImGui::SameLine();
    if (ImGui::Button("Spawn Elite B")) { spawn = 1; spawnAffixes = mask; }
    ImGui::SameLine();
    if (ImGui::Button("Move Now")) DebugReady();

    for (int t = 0; t < (int)EliteType::Count; ++t)
    {
        ImGui::PushID(t);
        if (ImGui::TreeNode(TypeLabel((EliteType)t)))
        {
            TypeTuning& tt = tune[t];
            ImGui::DragFloat("HP x", &tt.hpMul, 0.01f, 0.1f, 10.0f);
            ImGui::DragFloat("Walk Speed", &tt.speed, 0.05f, 0.0f, 12.0f);
            ImGui::DragFloat("Body Scale", &tt.scale, 0.01f, 0.5f, 4.0f);
            ImGui::DragFloat("Trigger Distance", &tt.trigger, 0.1f, 0.0f, 40.0f);
            ImGui::DragFloat("Wind-up (s)", &tt.windup, 0.01f, 0.05f, 5.0f);
            ImGui::DragFloat("Active (s)", &tt.active, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Recover (s)", &tt.recover, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Cooldown (s)", &tt.cooldown, 0.05f, 0.5f, 30.0f);
            ImGui::DragFloat("Damage", &tt.damage, 0.5f, 0.0f, 500.0f);
            ImGui::DragFloat("Radius / Length", &tt.radius, 0.05f, 0.1f, 40.0f);
            ImGui::DragFloat("Width / Half Angle", &tt.width, 0.05f, 0.0f, 90.0f);
            ImGui::DragFloat("Speed (dash / arrow / orb / turn)", &tt.speedMove, 0.1f, 0.0f, 100.0f);
            ImGui::DragInt("Count", &tt.count, 1, 0, 9);
            ImGui::DragFloat("Spread (deg)", &tt.spreadDeg, 0.1f, 0.0f, 60.0f);
            ImGui::DragFloat("Extra", &tt.extra, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloatRange2("Keep (ranged)", &tt.keepMin, &tt.keepMax, 0.1f, 0.0f, 30.0f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (ImGui::TreeNode("Affixes"))
    {
        ImGui::DragFloat("HP + per affix", &affixHpMul, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Iron: break below hp", &shieldBreakRatio, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Thunder interval", &thunderInterval, 0.05f, 0.5f, 20.0f);
        ImGui::DragInt("Thunder count", &thunderCount, 1, 1, 12);
        ImGui::DragFloat("Thunder warn (s)", &thunderWarn, 0.01f, 0.1f, 3.0f);
        ImGui::DragFloat("Thunder radius", &thunderRadius, 0.01f, 0.2f, 5.0f);
        ImGui::DragFloat("Thunder spread", &thunderSpread, 0.05f, 0.5f, 20.0f);
        ImGui::DragFloat("Thunder damage", &thunderDamage, 0.5f, 0.0f, 200.0f);
        ImGui::DragFloat("Berserk below hp", &enrageRatio, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Berserk cooldown x", &enrageCooldownMul, 0.01f, 0.1f, 1.0f);
        ImGui::DragFloat("Orb life", &orbLife, 0.05f, 0.5f, 20.0f);
        ImGui::DragFloat("Orb turn (deg/s)", &orbTurnDeg, 1.0f, 0.0f, 720.0f);
        ImGui::DragFloat("Orb hit radius", &orbHitRadius, 0.01f, 0.1f, 4.0f);
        ImGui::TreePop();
    }
    ImGui::TreePop();
}
