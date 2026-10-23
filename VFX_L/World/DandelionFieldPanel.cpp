// ============================================================
// DandelionFieldPanel.cpp
// DandelionField の ImGui（戦闘シーンのパネル「Dandelions (wind)」）
// ============================================================
#include "World/DandelionField.h"

#include <imgui.h>

void DandelionField::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Dandelions (wind)")) return;
    auto& s = m_Settings;
    ImGui::Text(m_Active ? "this stage grows them (grassland)" : "not on this stage (grassland only)");
    ImGui::Checkbox("Enabled##dl", &s.enabled);
    ImGui::SliderInt("Lattice Side", &s.side, 16, 160);
    ImGui::SliderInt("Seeds per Head", &s.seedsPer, 1, 24);
    bool replace = false;
    replace |= ImGui::SliderFloat("Window (half, m)##dl", &s.window, 8.0f, 60.0f);
    replace |= ImGui::SliderFloat("Density in Patch", &s.density, 0.0f, 1.0f);
    replace |= ImGui::SliderFloat("Patch Size (m)", &s.patchScale, 2.0f, 60.0f);
    replace |= ImGui::SliderFloat("Patch Cover", &s.patchCover, 0.0f, 1.0f);
    ImGui::DragFloatRange2("Stem Height (m)", &s.stemMin, &s.stemMax, 0.01f, 0.1f, 2.0f);
    ImGui::SliderFloat("Head Radius (m)", &s.puffRadius, 0.02f, 0.4f);
    ImGui::SliderFloat("Seed Size (m)", &s.seedSize, 0.01f, 0.2f);
    ImGui::SliderFloat("Size x##dl", &s.sizeMul, 0.3f, 3.0f);
    ImGui::SeparatorText("Blowing apart");
    ImGui::SliderFloat("Weather Wind (m/s)", &s.releaseWind, 0.5f, 10.0f);
    ImGui::SliderFloat("Local Wind (m/s)", &s.releaseLocal, 0.2f, 10.0f);
    ImGui::SliderFloat("Trample (0..1)", &s.releaseTrample, 0.05f, 1.0f);
    ImGui::SliderFloat("Player Brush Radius (m)", &s.playerRadius, 0.0f, 4.0f);
    ImGui::SliderFloat("Regrow Delay (s)", &s.regrowDelay, 0.0f, 60.0f);
    ImGui::SliderFloat("Regrow Time (s)", &s.regrowTime, 0.1f, 10.0f);
    ImGui::SeparatorText("Seeds");
    ImGui::SliderFloat("Drag (1/s)##dl", &s.seedDrag, 0.1f, 8.0f);
    ImGui::SliderFloat("Gravity##dl", &s.seedGravity, 0.0f, 5.0f);
    ImGui::DragFloatRange2("Life (s)##dl", &s.seedLifeMin, &s.seedLifeMax, 0.1f, 0.5f, 20.0f);
    ImGui::SliderFloat("Updraft x##dl", &s.seedLift, 0.0f, 4.0f);
    ImGui::SliderFloat("Stem Bend / (m/s)", &s.bendGain, 0.0f, 0.3f);
    replace |= ImGui::ColorEdit3("Stem Color##dl", &s.stemColor.x);
    ImGui::ColorEdit3("Head Color##dl", &s.puffColor.x);
    if (ImGui::Button("Re-seat all##dl") || replace)
        m_ResetPending = true;
}
