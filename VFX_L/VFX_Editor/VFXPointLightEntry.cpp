// ============================================================
// VFXPointLightEntry.cpp
// ============================================================
#include "VFX_Editor/VFXPointLightEntry.h"
#include "Graphics/Light/PointLightManager.h"
#include "imgui.h"
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    json V3(const Vector3& v) { return { v.x, v.y, v.z }; }
    Vector3 J3(const json& j, const Vector3& d) { return j.is_array() && j.size() >= 3 ? Vector3(j[0], j[1], j[2]) : d; }
}

float VFXPointLightEntry::Progress() const
{
    if (duration <= 0.0f) return 0.0f;
    float p = m_Age / duration;
    return (p < 0.0f) ? 0.0f : (p > 1.0f) ? 1.0f : p;
}

void VFXPointLightEntry::OnPlay(const VFXContext&)
{
    isPlaying = true;
    m_Age = 0.0f;
}

void VFXPointLightEntry::OnStop(const VFXContext&)
{
    isPlaying = false;
}

void VFXPointLightEntry::OnUpdate(float dt, const VFXContext&)
{
    m_Age += dt;
}

void VFXPointLightEntry::Submit(const Vector3& worldOffset)
{
    if (!isPlaying) return;

    float intensity = intensityStart + (intensityEnd - intensityStart) * Progress();
    if (flicker > 0.0f)
    {
        // 2 つの正弦を重ねた安い揺らぎ（乱数だとフレーム毎にちらつき過ぎる）
        const float t = m_Age * flickerSpeed;
        const float n = 0.5f * (std::sin(t * 6.2832f) + std::sin(t * 4.1f + 1.3f)) * 0.5f;
        intensity *= 1.0f + flicker * n;
    }
    PointLightManager::Get().Add(worldOffset + offset, color, radius, intensity);
}

void VFXPointLightEntry::OnImGui()
{
    ImGui::DragFloat3("Offset", &offset.x, 0.05f);
    ImGui::ColorEdit3("Color", &color.x, ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Radius", &radius, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("Intensity Start", &intensityStart, 0.1f, 0.0f, 50.0f);
    ImGui::DragFloat("Intensity End", &intensityEnd, 0.1f, 0.0f, 50.0f);
    ImGui::DragFloat("Flicker", &flicker, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Flicker Speed", &flickerSpeed, 0.5f, 0.0f, 60.0f);
    ImGui::TextDisabled("GPU projectiles use Intensity Start only (no timeline)");
}

std::unique_ptr<VFXEntry> VFXPointLightEntry::Clone() const
{
    auto c = std::make_unique<VFXPointLightEntry>();
    *c = *this;
    c->isPlaying = false;
    c->m_Age = 0.0f;
    return c;
}

json VFXPointLightEntry::ToJson() const
{
    return {
        { "offset", V3(offset) },
        { "color", V3(color) },
        { "radius", radius },
        { "intensityStart", intensityStart },
        { "intensityEnd", intensityEnd },
        { "flicker", flicker },
        { "flickerSpeed", flickerSpeed },
    };
}

void VFXPointLightEntry::FromJson(const json& j)
{
    offset = J3(j.value("offset", json()), offset);
    color = J3(j.value("color", json()), color);
    radius = j.value("radius", radius);
    intensityStart = j.value("intensityStart", intensityStart);
    intensityEnd = j.value("intensityEnd", intensityEnd);
    flicker = j.value("flicker", flicker);
    flickerSpeed = j.value("flickerSpeed", flickerSpeed);
}
