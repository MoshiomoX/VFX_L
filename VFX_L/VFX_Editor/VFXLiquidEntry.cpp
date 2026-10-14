// ============================================================
// VFXLiquidEntry.cpp
// ============================================================
#include "VFX_Editor/VFXLiquidEntry.h"
#include "VFX_Editor/VFXLiquidRenderer.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    json V3(const Vector3& v) { return { v.x, v.y, v.z }; }
    json V4(const Vector4& v) { return { v.x, v.y, v.z, v.w }; }
    Vector3 J3(const json& j, const Vector3& d) { return j.is_array() && j.size() >= 3 ? Vector3(j[0], j[1], j[2]) : d; }
    Vector4 J4(const json& j, const Vector4& d) { return j.is_array() && j.size() >= 4 ? Vector4(j[0], j[1], j[2], j[3]) : d; }
}

// ============================================================
// VFXLiquidDef（保存・ImGui）
// ============================================================
json VFXLiquidDef::ToJson() const
{
    return {
        { "deepColor", V4(deepColor) }, { "edgeColor", V4(edgeColor) },
        { "glowColor", V4(glowColor) }, { "skyColor", V4(skyColor) },
        { "radius", radius }, { "fill", fill }, { "spread", spread }, { "wobble", wobble },
        { "blend", blend }, { "rimWidth", rimWidth }, { "depthWidth", depthWidth }, { "bump", bump },
        { "splashTime", splashTime }, { "dryTime", dryTime }, { "specGain", specGain }, { "roughness", roughness },
        { "bubbles", bubbles }, { "bubbleGain", bubbleGain }, { "swirl", swirl }, { "swirlScale", swirlScale },
        { "lift", lift }, { "cliff", cliff }, { "haloWidth", haloWidth }, { "haloGain", haloGain },
        { "lobes", lobes }, { "spatter", spatter }, { "pointGain", pointGain },
    };
}

void VFXLiquidDef::FromJson(const json& j)
{
    deepColor = J4(j.value("deepColor", json()), deepColor);
    edgeColor = J4(j.value("edgeColor", json()), edgeColor);
    glowColor = J4(j.value("glowColor", json()), glowColor);
    skyColor = J4(j.value("skyColor", json()), skyColor);
    radius = j.value("radius", radius);
    fill = j.value("fill", fill);
    spread = j.value("spread", spread);
    wobble = j.value("wobble", wobble);
    blend = j.value("blend", blend);
    rimWidth = j.value("rimWidth", rimWidth);
    depthWidth = j.value("depthWidth", depthWidth);
    bump = j.value("bump", bump);
    splashTime = j.value("splashTime", splashTime);
    dryTime = j.value("dryTime", dryTime);
    specGain = j.value("specGain", specGain);
    roughness = j.value("roughness", roughness);
    bubbles = j.value("bubbles", bubbles);
    bubbleGain = j.value("bubbleGain", bubbleGain);
    swirl = j.value("swirl", swirl);
    swirlScale = j.value("swirlScale", swirlScale);
    lift = j.value("lift", lift);
    cliff = j.value("cliff", cliff);
    haloWidth = j.value("haloWidth", haloWidth);
    haloGain = j.value("haloGain", haloGain);
    lobes = (std::min)(j.value("lobes", lobes), 7u);
    spatter = (std::min)(j.value("spatter", spatter), 8u);
    pointGain = j.value("pointGain", pointGain);
}

void VFXLiquidDef::DrawImGui()
{
    ImGui::SeparatorText("Shape (SDF)");
    ImGui::DragFloat("Radius (m, 0 = area)", &radius, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("Fill", &fill, 0.01f, 0.1f, 1.0f);
    int lb = (int)lobes, sp = (int)spatter;
    if (ImGui::SliderInt("Lobes", &lb, 0, 7)) lobes = (uint32_t)lb;
    if (ImGui::SliderInt("Spatter", &sp, 0, 8)) spatter = (uint32_t)sp;
    ImGui::DragFloat("Spread (forward)", &spread, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Blend (smooth union)", &blend, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Wobble", &wobble, 0.002f, 0.0f, 0.2f);

    ImGui::SeparatorText("Time");
    ImGui::DragFloat("Splash Time (s)", &splashTime, 0.01f, 0.01f, 2.0f);
    ImGui::DragFloat("Dry Time (s)", &dryTime, 0.01f, 0.01f, 5.0f);

    ImGui::SeparatorText("Surface");
    ImGui::ColorEdit4("Deep Color", &deepColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::ColorEdit4("Edge Color", &edgeColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::DragFloat("Depth Width (m)", &depthWidth, 0.01f, 0.01f, 3.0f);
    ImGui::DragFloat("Rim Width (m)", &rimWidth, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Edge Bump (m)", &bump, 0.002f, 0.0f, 0.5f);
    ImGui::DragFloat("Specular", &specGain, 0.02f, 0.0f, 10.0f);
    ImGui::DragFloat("Roughness", &roughness, 0.005f, 0.04f, 1.0f);
    ImGui::DragFloat("Point Lights", &pointGain, 0.01f, 0.0f, 2.0f);   // 点光源の拡散 + 照り返しの効き
    ImGui::ColorEdit4("Sky Reflection (a = gain)", &skyColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::DragFloat("Swirl", &swirl, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Swirl Scale (1/m)", &swirlScale, 0.05f, 0.05f, 10.0f);
    ImGui::DragFloat("Bubbles", &bubbles, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Bubble Gain", &bubbleGain, 0.01f, 0.0f, 3.0f);

    ImGui::SeparatorText("Glow");
    ImGui::ColorEdit4("Glow (a = rim x)", &glowColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::DragFloat("Halo Width (m)", &haloWidth, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Halo Gain", &haloGain, 0.01f, 0.0f, 5.0f);

    ImGui::SeparatorText("Ground");
    ImGui::DragFloat("Lift (m)", &lift, 0.001f, 0.0f, 0.3f);
    ImGui::DragFloat("Cliff Cut (m)", &cliff, 0.01f, 0.05f, 5.0f);
}

// ============================================================
// 再生
// ============================================================
void VFXLiquidEntry::OnPlay(const VFXContext&)
{
    isPlaying = true;
    m_Age = 0.0f;
    m_Seed = (uint32_t)rand() * 2654435761u + (uint32_t)rand();   // 出る度に違う形
}

void VFXLiquidEntry::OnStop(const VFXContext&)
{
    isPlaying = false;
}

void VFXLiquidEntry::OnUpdate(float dt, const VFXContext&)
{
    m_Age += dt;
}

// ============================================================
// 描画に積む。乾く時間は entry の duration の末尾（-1 = ずっと乾かない）
// ============================================================
void VFXLiquidEntry::Submit(VFXLiquidRenderer& renderer, const Vector3& worldOffset)
{
    if (!isPlaying) return;

    VFXLiquidInstance inst;
    const Vector3 c = worldOffset + offset;
    inst.position[0] = c.x; inst.position[1] = c.y; inst.position[2] = c.z;
    inst.radius = (def.radius > 0.0f) ? def.radius : 2.0f;
    inst.dir[0] = previewDir.x;
    inst.dir[1] = previewDir.z;
    inst.age = m_Age;
    inst.left = (duration > 0.0f) ? (std::max)(duration - m_Age, 0.0f) : 1.0e4f;
    inst.seed = m_Seed;
    renderer.Submit(inst, def);
}

// ============================================================
// ImGui
// ============================================================
void VFXLiquidEntry::OnImGui()
{
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1), "Liquid (a puddle on the ground, SDF droplets)");
    ImGui::TextDisabled("GPU areas: drawn while the area lives, thrown-from direction, dries before it ends");
    ImGui::DragFloat3("Offset", &offset.x, 0.05f);
    ImGui::DragFloat3("Preview Dir (CPU)", &previewDir.x, 0.05f, -1.0f, 1.0f);
    if (ImGui::Button("New shape")) m_Seed = (uint32_t)rand() * 2654435761u + (uint32_t)rand();
    ImGui::SameLine();
    ImGui::TextDisabled("seed %u", m_Seed);
    def.DrawImGui();
}

// ============================================================
// 複製・保存
// ============================================================
std::unique_ptr<VFXEntry> VFXLiquidEntry::Clone() const
{
    auto e = std::make_unique<VFXLiquidEntry>(*this);
    e->isPlaying = false;
    e->m_Age = 0.0f;
    return e;
}

json VFXLiquidEntry::ToJson() const
{
    json j = def.ToJson();
    j["offset"] = V3(offset);
    j["previewDir"] = V3(previewDir);
    return j;
}

void VFXLiquidEntry::FromJson(const json& j)
{
    def.FromJson(j);
    offset = J3(j.value("offset", json()), offset);
    previewDir = J3(j.value("previewDir", json()), previewDir);
}
