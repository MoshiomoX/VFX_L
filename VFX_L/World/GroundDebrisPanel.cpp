// ============================================================
// GroundDebrisPanel.cpp
// 地面の落ち葉の ImGui パネル（Grass Fire の後ろの「Ground Debris」）
// ============================================================
#include "World/GroundDebris.h"
#include "imgui.h"

void GroundDebris::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Ground Debris")) return;

    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##debris", &s.enabled);
    ImGui::SameLine();
    ImGui::Text("pieces %d (%d x %d)%s", Count(), m_BufSide, m_BufSide, m_Ready ? "" : "  [not built]");
    ImGui::TextDisabled("VFXL_NO_DEBRIS=1 turns it off at start");

    ImGui::SeparatorText("Layout");
    int side = s.side;
    if (ImGui::SliderInt("Lattice Side", &side, 16, 160)) s.side = side;   // Render が buffer を作り直す
    ImGui::DragFloat("Window", &s.window, 0.5f, 8.0f, 80.0f, "%.0f m");
    ImGui::Text("spacing %.2f m", 2.0f * s.window / (float)(std::max)(s.side, 1));
    ImGui::SliderFloat("Density", &s.density, 0.0f, 1.0f);
    ImGui::DragFloat("Size x", &s.sizeMul, 0.02f, 0.2f, 4.0f);
    ImGui::DragFloat("Edge Fade", &s.fadeWidth, 0.1f, 0.5f, 20.0f, "%.1f m");
    ImGui::DragFloat("Max Slope##debris", &s.maxSlope, 0.02f, 0.1f, 5.0f);
    if (ImGui::Button("Re-seat all")) m_ResetPending = true;

    ImGui::SeparatorText("Motion");
    ImGui::DragFloat("Lift Wind", &s.liftWind, 0.05f, 0.1f, 20.0f, "%.2f m/s");
    ImGui::TextDisabled("world wind now %.2f .. %.2f m/s", m_Wind.speed * m_Wind.strength,
        (m_Wind.speed + m_Wind.gust) * m_Wind.strength);
    ImGui::DragFloat("Gravity##debris", &s.gravity, 0.1f, 0.0f, 30.0f);
    ImGui::DragFloat("Drag##debris", &s.drag, 0.05f, 0.0f, 20.0f, "%.2f 1/s");
    ImGui::Text("terminal fall %.2f m/s", s.drag > 0.0f ? s.gravity / s.drag : 0.0f);
    ImGui::DragFloat("Gust Air Speed", &s.gustSpeed, 0.1f, 0.0f, 40.0f, "%.1f m/s");
    ImGui::DragFloat("Gust Time", &s.gustTime, 0.01f, 0.05f, 2.0f, "%.2f s");
    ImGui::DragFloat("Blast Accel", &s.blastAccel, 1.0f, 0.0f, 300.0f, "%.0f m/s^2");
    ImGui::Text("active gusts %d", (int)m_Gusts.size());

    ImGui::SeparatorText("Colors (per stage, set at Build)");
    ImGui::ColorEdit3("Leaf", &s.colorLeaf.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit3("Clipping", &s.colorClip.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit3("Twig", &s.colorTwig.x, ImGuiColorEditFlags_Float);
}
