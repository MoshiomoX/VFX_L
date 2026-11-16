// ============================================================
// WindTreesPanel.cpp
// 風で揺れる木の ImGui パネル（Ground Debris の後ろの「Wind Trees」）
// ============================================================
#include "World/WindTrees.h"
#include "imgui.h"

void WindTrees::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Wind Trees")) return;

    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##wtrees", &s.enabled);
    ImGui::SameLine();
    ImGui::Text("trees %d / bare %d / shrubs %d%s", m_Stats.trees, m_Stats.bare, m_Stats.shrubs, m_Ready ? "" : "  [not built]");
    ImGui::Text("vertices %d, leaves %d", m_Stats.vertices, m_Stats.leaves);

    ImGui::SeparatorText("Shape");
    ImGui::DragFloat("Leaf Density", &s.leafDensity, 0.02f, 0.1f, 4.0f);
    ImGui::DragFloat("Leaf Size", &s.leafSize, 0.02f, 0.2f, 4.0f);
    ImGui::DragFloat("Tree Height x", &s.treeHeightMul, 0.01f, 0.5f, 3.0f);
    ImGui::DragFloat("Shrub Flex", &s.shrubFlex, 0.01f, 0.05f, 2.0f);
    if (ImGui::Button("Rebuild##wtrees")) Rebuild();

    ImGui::SeparatorText("Sway");
    ImGui::DragFloat("Lean / wind^2", &s.leanPerWind2, 0.0002f, 0.0f, 0.05f, "%.4f");
    ImGui::DragFloat("Max Lean", &s.maxLean, 0.005f, 0.0f, 0.45f);
    ImGui::DragFloat("Frequency (5 m)", &s.freq, 0.01f, 0.05f, 3.0f, "%.2f Hz");
    ImGui::DragFloat("Damping##wtrees", &s.damping, 0.005f, 0.0f, 2.0f);
    ImGui::DragFloat("Wind Ref", &s.windRef, 0.05f, 0.1f, 20.0f, "%.2f m/s");
    ImGui::DragFloat("Turbulence", &s.turbulence, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Blast Accel##wtrees", &s.blastAccel, 1.0f, 0.0f, 400.0f, "%.0f m/s^2");
    ImGui::DragFloat("Shake Decay", &s.shakeDecay, 0.05f, 0.1f, 20.0f);
    ImGui::DragFloat("Gust Air Speed##wtrees", &s.gustSpeed, 0.1f, 0.0f, 40.0f, "%.1f m/s");

    ImGui::SeparatorText("Branches / Leaves");
    ImGui::DragFloat("Branch Idle", &s.branchIdle, 0.002f, 0.0f, 0.5f, "%.3f m");
    ImGui::DragFloat("Branch Wind", &s.branchWind, 0.005f, 0.0f, 1.0f, "%.3f m");
    ImGui::DragFloat("Flutter Idle", &s.flutterIdle, 0.005f, 0.0f, 2.0f, "%.3f rad");
    ImGui::DragFloat("Flutter Wind", &s.flutterWind, 0.005f, 0.0f, 3.0f, "%.3f rad");
    ImGui::DragFloat("Shake Amp", &s.shakeAmp, 0.005f, 0.0f, 1.0f, "%.3f m");
    ImGui::SliderFloat("Crown Normal Mix", &s.crownMix, 0.0f, 1.0f);
    ImGui::SliderFloat("Inner Dark", &s.innerDark, 0.0f, 1.0f);
    ImGui::SliderFloat("Leaf Width", &s.leafWidth, 0.1f, 1.0f);
    ImGui::SliderInt("Shadow Leaf Thin", &s.shadowThin, 1, 4);
    ImGui::ColorEdit3("Bark", &s.barkColor.x);
    ImGui::ColorEdit3("Bare Bark", &s.bareColor.x);
    ImGui::ColorEdit3("Leaf", &s.leafColor.x);
    ImGui::ColorEdit3("Leaf 2", &s.leafColor2.x);
}
