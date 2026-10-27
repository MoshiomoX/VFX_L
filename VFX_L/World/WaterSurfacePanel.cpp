// ============================================================
// WaterSurfacePanel.cpp
// 水面の ImGui パネル（Ground Debris の後ろの「Water」）
// ============================================================
#include "World/WaterSurface.h"
#include "imgui.h"

void WaterSurface::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Water (ponds)")) return;

    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##water", &s.enabled);
    ImGui::SameLine();
    ImGui::Text("pond cells %u, triangles %u", m_PondCells, m_VertexCount / 3);
    ImGui::TextDisabled("VFXL_NO_WATER=1 turns it off at start");
    ImGui::TextDisabled("rain %.2f  sandstorm %.2f  wind %.2f .. %.2f m/s", m_Rain, m_Turbid,
        m_Wind.speed * m_Wind.strength, (m_Wind.speed + m_Wind.gust) * m_Wind.strength);

    ImGui::SeparatorText("Look");
    ImGui::ColorEdit3("Shallow", &s.shallowColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::SliderFloat("Shallow Alpha", &s.shallowAlpha, 0.0f, 1.0f);
    ImGui::ColorEdit3("Deep", &s.deepColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::SliderFloat("Deep Alpha", &s.deepAlpha, 0.0f, 1.0f);
    ImGui::DragFloat("Deep Distance", &s.deepDist, 0.05f, 0.1f, 8.0f, "%.2f m");
    ImGui::ColorEdit3("Foam", &s.foamColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::DragFloat("Foam Width", &s.foamWidth, 0.01f, 0.0f, 1.0f, "%.2f m");
    ImGui::ColorEdit3("Muddy (sandstorm)", &s.turbidColor.x, ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Sky Reflection", &s.skyReflect, 0.0f, 1.0f);
    ImGui::DragFloat("Sun Glint", &s.specStrength, 0.05f, 0.0f, 10.0f);

    ImGui::SeparatorText("Wind ripples (only where the air moves)");
    ImGui::DragFloat("Calm Below", &s.calmWind, 0.05f, 0.0f, 20.0f, "%.2f m/s");
    ImGui::DragFloat("Full At", &s.fullWind, 0.05f, 0.1f, 30.0f, "%.2f m/s");
    ImGui::DragFloat("Tilt##wave", &s.waveNormal, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Fineness", &s.waveScale, 0.02f, 0.1f, 8.0f, "%.2f / m");
    ImGui::DragFloat("Drift", &s.waveFlow, 0.01f, 0.0f, 2.0f, "%.2f x wind");
    ImGui::SliderFloat("Darken", &s.windDarken, 0.0f, 0.6f);

    ImGui::SeparatorText("Ripples (wave field)");
    ImGui::DragFloat("Wave Speed", &s.rippleSpeed, 0.01f, 0.0f, 4.0f, "%.2f m/s");
    ImGui::DragFloat("Damping", &s.rippleDamping, 0.01f, 0.0f, 6.0f, "%.2f 1/s");
    ImGui::DragFloat("Tilt##ripple", &s.rippleNormal, 0.05f, 0.0f, 30.0f);
    ImGui::DragFloat("Crest", &s.rippleCrest, 0.0005f, 0.0005f, 0.1f, "%.4f m");
    ImGui::DragFloat("Step", &s.stepStrength, 0.002f, 0.0f, 0.5f, "%.3f m");
    ImGui::DragFloat("Step Radius", &s.stepRadius, 0.01f, 0.05f, 2.0f, "%.2f m");
    ImGui::DragFloat("Wade", &s.wadeStrength, 0.001f, 0.0f, 0.1f, "%.3f");
    ImGui::DragFloat("Landing", &s.landStrength, 0.005f, 0.0f, 1.0f, "%.3f m");
    ImGui::DragFloat("Landing Radius", &s.landRadius, 0.01f, 0.1f, 3.0f, "%.2f m");
    ImGui::DragFloat("Rain Drops", &s.rainRate, 0.05f, 0.0f, 20.0f, "%.2f / m^2 s");
    ImGui::DragFloat("Rain Drop", &s.rainStrength, 0.001f, 0.0f, 0.2f, "%.3f m");
    ImGui::DragFloat("Blast", &s.blastStrength, 0.005f, 0.0f, 1.0f, "%.3f m");
    ImGui::DragFloat("Other Areas", &s.areaStrength, 0.005f, 0.0f, 1.0f, "%.3f m");
    if (ImGui::Button("Calm the water")) ClearRipples();
}
