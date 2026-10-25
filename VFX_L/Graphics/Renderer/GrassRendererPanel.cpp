// ============================================================
// GrassRendererPanel.cpp
// GrassRenderer の ImGui パネルと、自動テスト用の踏み跡の読み戻し
// （2026-10-23、草の細部を足して GrassRenderer.cpp が 500 行を越えるので分けた）
// ============================================================
#include "Graphics/Renderer/GrassRenderer.h"
#include "imgui.h"

#include <DirectXPackedVector.h>

using DirectX::SimpleMath::Vector2;

// TEMP-TEST: 踏み跡の読み戻し（自動テスト envfx）
bool GrassRenderer::DebugTrampleStats(const Vector2& centerXZ, float radius, float minStrength, int& count, float& maxStrength)
{
    count = 0;
    maxStrength = 0.0f;
    if (!m_Context || !m_Device || !m_TrampleTex[m_TrampleCur]) return false;
    if (!m_TrampleStaging)
    {
        D3D11_TEXTURE2D_DESC td = {};
        m_TrampleTex[0]->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_TrampleStaging))) return false;
    }
    m_Context->CopyResource(m_TrampleStaging.Get(), m_TrampleTex[m_TrampleCur].Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_TrampleStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const float texel = m_MapSize.x / (float)kTrampleSize;
    const int cx = (int)((centerXZ.x - m_MapOrigin.x) / texel), cy = (int)((centerXZ.y - m_MapOrigin.y) / texel);
    const int r = (int)std::ceil(radius / texel);
    for (int y = (std::max)(cy - r, 0); y <= (std::min)(cy + r, kTrampleSize - 1); ++y)
    {
        const auto* row = reinterpret_cast<const DirectX::PackedVector::HALF*>(
            static_cast<const uint8_t*>(mapped.pData) + (size_t)y * mapped.RowPitch);
        for (int x = (std::max)(cx - r, 0); x <= (std::min)(cx + r, kTrampleSize - 1); ++x)
        {
            const float wx = m_MapOrigin.x + ((float)x + 0.5f) * texel, wz = m_MapOrigin.y + ((float)y + 0.5f) * texel;
            if ((wx - centerXZ.x) * (wx - centerXZ.x) + (wz - centerXZ.y) * (wz - centerXZ.y) > radius * radius) continue;
            const float vx = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 2]);
            const float vy = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 2 + 1]);
            const float s = std::sqrt(vx * vx + vy * vy);
            if (s >= minStrength) ++count;
            maxStrength = (std::max)(maxStrength, s);
        }
    }
    m_Context->Unmap(m_TrampleStaging.Get(), 0);
    return true;
}

void GrassRenderer::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Grass")) return;

    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##grass", &s.enabled);
    ImGui::SameLine();
    ImGui::Text("blades %u / %u", m_LastCount, kMaxBlades);
    ImGui::DragFloat("Spacing", &s.spacing, 0.005f, 0.05f, 1.0f, "%.3f m");
    ImGui::DragFloat("Max Distance##grass", &s.maxDistance, 0.5f, 5.0f, 150.0f, "%.0f m");
    ImGui::DragFloat("Full Density To", &s.fullDensityDistance, 0.5f, 0.0f, 150.0f, "%.0f m");
    ImGui::SliderFloat("Far Density", &s.farDensity, 0.0f, 1.0f);
    ImGui::DragFloatRange2("Height##grass", &s.heightMin, &s.heightMax, 0.005f, 0.02f, 2.0f, "%.2f m");
    ImGui::DragFloatRange2("Width##grass", &s.widthMin, &s.widthMax, 0.001f, 0.005f, 0.3f, "%.3f m");
    ImGui::SliderFloat("Curl", &s.curlMax, 0.0f, 0.9f);
    ImGui::DragFloat("Max Slope##grass", &s.maxSlope, 0.02f, 0.1f, 5.0f);
    ImGui::ColorEdit3("Root Color", &s.rootColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::ColorEdit3("Tip Color", &s.tipColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::SeparatorText("Wind");
    ImGui::SliderFloat("Bend at wind x1", &s.windStrength, 0.0f, 1.0f);
    ImGui::TextDisabled("direction / speed / waves: Weather panel (World Wind), strength now x%.2f", m_Wind.strength);
    ImGui::SeparatorText("Fire (GrassFireField)");
    ImGui::SliderFloat("Burnt Shrink", &s.fireShrink, 0.0f, 0.95f);
    ImGui::DragFloat("Ember Glow", &s.fireGlow, 0.05f, 0.0f, 10.0f);
    ImGui::SeparatorText("Trample");
    ImGui::DragFloat("Radius##trample", &s.trampleRadius, 0.01f, 0.0f, 3.0f, "%.2f m");
    ImGui::DragFloat("Slide Radius", &s.trampleSlideRadius, 0.01f, 0.0f, 3.0f, "%.2f m");
    ImGui::DragFloat("Recover", &s.trampleRecover, 0.05f, 0.05f, 20.0f, "%.2f s");
    ImGui::SliderFloat("Bend##trample", &s.trampleBend, 0.0f, 1.0f);
    ImGui::SeparatorText("Trample from the swarm");
    ImGui::DragFloat("Blast Radius x", &s.blastTrample, 0.02f, 0.0f, 3.0f);
    ImGui::DragFloat("Beam Band x radius", &s.beamTrample, 0.05f, 0.0f, 5.0f);
    ImGui::SliderFloat("Enemy Feet Strength", &s.enemyTrample, 0.0f, 1.0f);
    ImGui::DragFloat("Enemy Feet Radius", &s.enemyRadius, 0.01f, 0.05f, 1.5f, "%.2f m");
    ImGui::TextDisabled("areas %s  enemies %s", m_AreaSRV ? "on" : "off", m_EnemySRV ? "on" : "off");
    // ---- 細部（2026-10-23）----
    ImGui::SeparatorText("Gust waves + sheen (blades turned over)");
    ImGui::DragFloat("Wave 2 Angle", &s.wave2Angle, 1.0f, -180.0f, 180.0f, "%.0f deg");
    ImGui::DragFloat("Wave 2 Scale x", &s.wave2ScaleMul, 0.05f, 0.1f, 10.0f);
    ImGui::DragFloat("Wave 2 Speed x", &s.wave2SpeedMul, 0.05f, 0.0f, 10.0f);
    ImGui::SliderFloat("Wave 2 Weight", &s.wave2Weight, 0.0f, 1.0f);
    ImGui::ColorEdit3("Sheen Color", &s.sheenColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::SliderFloat("Sheen Amount", &s.sheenAmount, 0.0f, 1.5f);
    ImGui::DragFloat("Wind Preview x (TEMP)", &s.debugWindMul, 0.05f, 0.0f, 8.0f);
    ImGui::SeparatorText("Wind sources (twister / vortex / gust)");
    ImGui::DragFloat("Lay Speed", &s.vortexLaySpeed, 0.1f, 0.1f, 30.0f, "%.1f m/s");
    ImGui::DragFloat("Curl-up Updraft", &s.vortexLiftSpeed, 0.1f, 0.1f, 20.0f, "%.1f m/s");
    ImGui::SliderFloat("Flutter", &s.vortexFlutter, 0.0f, 0.8f);
    ImGui::SliderFloat("Curl Twist", &s.vortexTwist, 0.0f, 4.0f, "%.2f rad");
    ImGui::SliderFloat("Trail Strength", &s.trailStrength, 0.0f, 1.0f);
    ImGui::DragFloat("Trail Full Speed", &s.trailSpeed, 0.1f, 0.1f, 30.0f, "%.1f m/s");
    ImGui::TextDisabled("wind sources %s (the trail stands back up like a footprint: Recover)", m_WindSourceSRV ? "on" : "off");
    ImGui::SeparatorText("Blade shape");
    ImGui::SliderFloat("Own Twist", &s.twistMax, 0.0f, 2.0f, "%.2f rad");
    ImGui::SliderFloat("Dry Share", &s.dryShare, 0.0f, 0.5f);
    ImGui::ColorEdit3("Dry Color", &s.dryColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
}
