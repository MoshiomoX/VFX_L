// ============================================================
// VFXBeamEntry.cpp
// ============================================================
#include "VFX_Editor/VFXBeamEntry.h"
#include "VFX_Editor/VFXBeamRenderer.h"
#include "imgui.h"
#include <algorithm>
#include <cstdlib>

using namespace DirectX::SimpleMath;

namespace
{
    json V3(const Vector3& v) { return { v.x, v.y, v.z }; }
    json V4(const Vector4& v) { return { v.x, v.y, v.z, v.w }; }
    Vector3 J3(const json& j, const Vector3& d) { return j.is_array() && j.size() >= 3 ? Vector3(j[0], j[1], j[2]) : d; }
    Vector4 J4(const json& j, const Vector4& d) { return j.is_array() && j.size() >= 4 ? Vector4(j[0], j[1], j[2], j[3]) : d; }
}

// ============================================================
// 再生
// ============================================================
void VFXBeamEntry::OnPlay(const VFXContext&)
{
    isPlaying = true;
    m_Age = 0.0f;
    m_Seed = (float)(rand() % 1000);   // 本ごとにノイズをずらす（同時に 2 本出ても同じ模様にならない）
}

void VFXBeamEntry::OnStop(const VFXContext&)
{
    isPlaying = false;
}

void VFXBeamEntry::OnUpdate(float dt, const VFXContext&)
{
    m_Age += dt;
}

// ============================================================
// 描画に積む。伸びる / 縮むはここで起点・終点を動かして作る
// ============================================================
void VFXBeamEntry::Submit(VFXBeamRenderer& renderer, const Vector3& worldOffset,
    const Vector3& beamEnd, bool hasEnd)
{
    if (!isPlaying) return;

    const Vector3 start = worldOffset + offset;
    Vector3 dir = previewDir;
    if (dir.LengthSquared() < 1e-6f) dir = Vector3(0, 0, 1);
    dir.Normalize();
    const Vector3 end = hasEnd ? beamEnd : start + dir * previewLength;

    // 出る時は根元から先端へ、消える時は根元から縮む（duration の末尾 shrinkTime 秒）
    float t0 = 0.0f, t1 = 1.0f;
    if (growTime > 0.0f) t1 = std::clamp(m_Age / growTime, 0.0f, 1.0f);
    if (duration > 0.0f && shrinkTime > 0.0f)
    {
        const float left = duration - m_Age;
        if (left < shrinkTime) t0 = std::clamp(1.0f - left / shrinkTime, 0.0f, 1.0f);
    }
    if (t0 >= t1) return;

    VFXBeamItem item;
    item.start = start + (end - start) * t0;
    item.end = start + (end - start) * t1;
    item.width = width;
    item.coreRatio = coreRatio;
    item.color = color;
    item.coreColor = coreColor;
    item.glowRatio = glowRatio;
    item.glowAlpha = glowAlpha;
    item.scroll = m_Age * scrollSpeed;
    item.noiseScale = noiseScale;
    item.noiseStrength = noiseStrength;
    item.tipFade = tipFade;
    item.rootFade = rootFade;
    item.seed = m_Seed;
    renderer.Submit(item);
}

// ============================================================
// ImGui
// ============================================================
void VFXBeamEntry::OnImGui()
{
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Beam (start -> end ribbon, 3 layers)");
    ImGui::TextDisabled("the end comes from the game (WeaponSystem); the editor uses Preview Dir / Length");
    ImGui::DragFloat3("Offset", &offset.x, 0.05f);
    ImGui::DragFloat3("Preview Dir", &previewDir.x, 0.05f, -1.0f, 1.0f);
    ImGui::DragFloat("Preview Length (m)", &previewLength, 0.1f, 0.5f, 80.0f);
    ImGui::Separator();

    ImGui::DragFloat("Width (m)", &width, 0.01f, 0.02f, 10.0f);
    ImGui::DragFloat("Core Ratio", &coreRatio, 0.01f, 0.05f, 1.0f);
    ImGui::DragFloat("Glow Ratio", &glowRatio, 0.05f, 1.0f, 6.0f);
    ImGui::DragFloat("Glow Alpha", &glowAlpha, 0.01f, 0.0f, 2.0f);
    ImGui::ColorEdit4("Color (HDR)", &color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::ColorEdit4("Core Color", &coreColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::Separator();

    ImGui::DragFloat("Scroll Speed (m/s)", &scrollSpeed, 0.1f, -40.0f, 40.0f);
    ImGui::DragFloat("Noise Scale (1/m)", &noiseScale, 0.05f, 0.05f, 10.0f);
    ImGui::DragFloat("Noise Strength", &noiseStrength, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Tip Fade (m)", &tipFade, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Root Fade (m)", &rootFade, 0.05f, 0.0f, 10.0f);
    ImGui::Separator();

    ImGui::DragFloat("Grow Time (s)", &growTime, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Shrink Time (s)", &shrinkTime, 0.01f, 0.0f, 2.0f);
    ImGui::TextDisabled("grow: root -> tip when it appears. shrink: root -> tip over the last seconds of the duration");
}

// ============================================================
// 複製・保存
// ============================================================
std::unique_ptr<VFXEntry> VFXBeamEntry::Clone() const
{
    auto e = std::make_unique<VFXBeamEntry>(*this);
    e->isPlaying = false;
    e->m_Age = 0.0f;
    return e;
}

json VFXBeamEntry::ToJson() const
{
    return {
        { "offset", V3(offset) },
        { "width", width },
        { "coreRatio", coreRatio },
        { "glowRatio", glowRatio },
        { "glowAlpha", glowAlpha },
        { "color", V4(color) },
        { "coreColor", V4(coreColor) },
        { "scrollSpeed", scrollSpeed },
        { "noiseScale", noiseScale },
        { "noiseStrength", noiseStrength },
        { "tipFade", tipFade },
        { "rootFade", rootFade },
        { "growTime", growTime },
        { "shrinkTime", shrinkTime },
        { "previewLength", previewLength },
        { "previewDir", V3(previewDir) },
    };
}

void VFXBeamEntry::FromJson(const json& j)
{
    offset = J3(j.value("offset", json()), offset);
    width = j.value("width", width);
    coreRatio = j.value("coreRatio", coreRatio);
    glowRatio = j.value("glowRatio", glowRatio);
    glowAlpha = j.value("glowAlpha", glowAlpha);
    color = J4(j.value("color", json()), color);
    coreColor = J4(j.value("coreColor", json()), coreColor);
    scrollSpeed = j.value("scrollSpeed", scrollSpeed);
    noiseScale = j.value("noiseScale", noiseScale);
    noiseStrength = j.value("noiseStrength", noiseStrength);
    tipFade = j.value("tipFade", tipFade);
    rootFade = j.value("rootFade", rootFade);
    growTime = j.value("growTime", growTime);
    shrinkTime = j.value("shrinkTime", shrinkTime);
    previewLength = j.value("previewLength", previewLength);
    previewDir = J3(j.value("previewDir", json()), previewDir);
}
