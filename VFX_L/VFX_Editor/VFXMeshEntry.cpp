// ============================================================
// VFXMeshEntry.cpp
// ============================================================
#include "VFX_Editor/VFXMeshEntry.h"
#include "VFX_Editor/VFXFileList.h"
#include "VFX_Editor/VFXMeshRenderer.h"
#include "Particle/GPUParticleSystem.h"   // EdgeFilterParams
#include "Graphics/Material/Texture.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Model/Model.h"
#include "imgui.h"
#include <iostream>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr const char* kMeshDir = "Assets/VFX/Mesh";
    constexpr const char* kTexDir = "Assets/VFX/Tex";

    json V2(const Vector2& v) { return { v.x, v.y }; }
    json V3(const Vector3& v) { return { v.x, v.y, v.z }; }
    json V4(const Vector4& v) { return { v.x, v.y, v.z, v.w }; }
    Vector2 J2(const json& j, const Vector2& d) { return j.is_array() && j.size() >= 2 ? Vector2(j[0], j[1]) : d; }
    Vector3 J3(const json& j, const Vector3& d) { return j.is_array() && j.size() >= 3 ? Vector3(j[0], j[1], j[2]) : d; }
    Vector4 J4(const json& j, const Vector4& d) { return j.is_array() && j.size() >= 4 ? Vector4(j[0], j[1], j[2], j[3]) : d; }
}

void VFXMeshEntry::LoadModel()
{
    m_Model.reset();
    if (!modelPath.empty())
        m_Model = ResourceManager::Get().LoadModel(modelPath);
}

float VFXMeshEntry::Progress() const
{
    if (duration <= 0.0f) return 0.0f;
    float p = m_Age / duration;
    return (p < 0.0f) ? 0.0f : (p > 1.0f) ? 1.0f : p;
}

void VFXMeshEntry::OnPlay(const VFXContext&)
{
    isPlaying = true;
    m_Age = 0.0f;
    if (!m_Model) LoadModel();
}

void VFXMeshEntry::OnStop(const VFXContext&)
{
    isPlaying = false;
}

void VFXMeshEntry::OnUpdate(float dt, const VFXContext&)
{
    m_Age += dt;
}

// ============================================================
// 描画項目を積む。実際の描画は VFXMeshRenderer が粒子の前にまとめてやる
// ============================================================
void VFXMeshEntry::Submit(VFXMeshRenderer& renderer, const Vector3& worldOffset)
{
    if (!isPlaying || !m_Model) return;

    const float p = Progress();

    VFXMeshDrawItem item;
    item.model = m_Model.get();

    const Vector3 rot = rotationDeg + rotSpeedDeg * m_Age;
    const Vector3 scl = Vector3::Lerp(scaleStart, scaleEnd, p);
    item.world = Matrix::CreateScale(scl)
        * Matrix::CreateFromYawPitchRoll(
            DirectX::XMConvertToRadians(rot.y),
            DirectX::XMConvertToRadians(rot.x),
            DirectX::XMConvertToRadians(rot.z))
        * Matrix::CreateTranslation(worldOffset + offset);

    item.mainTex = mainTex.texture.get();
    item.noiseTex = noiseTex.texture.get();
    item.maskTex = maskTex.texture.get();

    item.params.tint = tint;
    item.params.tint.w *= alphaStart + (alphaEnd - alphaStart) * p;
    item.params.intensity = intensity;
    item.params.mainTiling = mainTiling;
    item.params.mainScroll = mainScroll * m_Age;
    item.params.noiseTiling = noiseTiling;
    item.params.noiseScroll = noiseScroll * m_Age;
    item.params.distortion = distortion;
    item.params.dissolveThreshold = dissolveEnabled
        ? dissolveStart + (dissolveEnd - dissolveStart) * p : -1.0f;   // 負 = 無効
    item.params.dissolveEdge = dissolveEdge;
    item.params.dissolveEdgeColor = dissolveEdgeColor;
    item.params.hasNoise = noiseTex.IsValid() ? 1u : 0u;
    item.params.hasMask = maskTex.IsValid() ? 1u : 0u;

    item.blend = blend;
    item.twoSided = twoSided;

    renderer.Submit(item);
}

// ============================================================
// 溶解の縁の粒子用（Submit と同じ式で閾値を出す）
// ============================================================
bool VFXMeshEntry::GetEdgeFilterParams(EdgeFilterParams& out) const
{
    if (!dissolveEnabled || !noiseTex.IsValid()) return false;

    const float p = Progress();
    out.noiseSRV = noiseTex.texture->GetSRV();
    out.noiseTiling = noiseTiling;
    out.noiseScroll = noiseScroll * m_Age;
    out.threshold = dissolveStart + (dissolveEnd - dissolveStart) * p;
    out.edge = dissolveEdge;
    return true;
}


// ============================================================
// ImGui
// ============================================================
void VFXMeshEntry::OnImGui()
{
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Model");
    if (VFXFileList::Combo("Mesh", kMeshDir, { ".fbx", ".obj", ".gltf", ".glb" }, modelPath))
        LoadModel();
    if (!m_Model && !modelPath.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "load failed");
    ImGui::Separator();

    mainTex.OnImGui("Main", kTexDir);
    noiseTex.OnImGui("Noise", kTexDir);
    maskTex.OnImGui("Mask", kTexDir);
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Transform");
    ImGui::DragFloat3("Offset", &offset.x, 0.05f);
    ImGui::DragFloat3("Rotation", &rotationDeg.x, 1.0f);
    ImGui::DragFloat3("Rot Speed", &rotSpeedDeg.x, 1.0f);
    ImGui::DragFloat3("Scale Start", &scaleStart.x, 0.01f);
    ImGui::DragFloat3("Scale End", &scaleEnd.x, 0.01f);
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Look");
    ImGui::ColorEdit4("Tint", &tint.x);
    ImGui::DragFloat("Alpha Start", &alphaStart, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Alpha End", &alphaEnd, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 20.0f);
    const char* blendNames[] = { "Additive", "Alpha" };
    ImGui::Combo("Blend", &blend, blendNames, 2);
    ImGui::Checkbox("Two Sided", &twoSided);
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "UV");
    ImGui::DragFloat2("Main Tiling", &mainTiling.x, 0.05f);
    ImGui::DragFloat2("Main Scroll", &mainScroll.x, 0.01f);
    ImGui::DragFloat2("Noise Tiling", &noiseTiling.x, 0.05f);
    ImGui::DragFloat2("Noise Scroll", &noiseScroll.x, 0.01f);
    ImGui::DragFloat("Distortion", &distortion, 0.005f, 0.0f, 1.0f);
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Dissolve");
    ImGui::Checkbox("Enabled", &dissolveEnabled);
    if (dissolveEnabled)
    {
        ImGui::DragFloat("Threshold Start", &dissolveStart, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Threshold End", &dissolveEnd, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Edge Width", &dissolveEdge, 0.005f, 0.0f, 0.5f);
        ImGui::ColorEdit4("Edge Color", &dissolveEdgeColor.x);
        if (!noiseTex.IsValid())
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "needs a Noise texture");
    }
}

// ============================================================
// json
// ============================================================
json VFXMeshEntry::ToJson() const
{
    json j;
    j["model"] = modelPath;
    j["mainTex"] = mainTex.ToJson();
    j["noiseTex"] = noiseTex.ToJson();
    j["maskTex"] = maskTex.ToJson();

    j["offset"] = V3(offset);
    j["rotation"] = V3(rotationDeg);
    j["rotSpeed"] = V3(rotSpeedDeg);
    j["scaleStart"] = V3(scaleStart);
    j["scaleEnd"] = V3(scaleEnd);

    j["tint"] = V4(tint);
    j["alphaStart"] = alphaStart;
    j["alphaEnd"] = alphaEnd;
    j["intensity"] = intensity;
    j["blend"] = blend;
    j["twoSided"] = twoSided;

    j["mainTiling"] = V2(mainTiling);
    j["mainScroll"] = V2(mainScroll);
    j["noiseTiling"] = V2(noiseTiling);
    j["noiseScroll"] = V2(noiseScroll);
    j["distortion"] = distortion;

    j["dissolve"] = dissolveEnabled;
    j["dissolveStart"] = dissolveStart;
    j["dissolveEnd"] = dissolveEnd;
    j["dissolveEdge"] = dissolveEdge;
    j["dissolveEdgeColor"] = V4(dissolveEdgeColor);
    return j;
}

void VFXMeshEntry::FromJson(const json& j)
{
    modelPath = j.value("model", "");
    if (j.contains("mainTex"))  mainTex.FromJson(j["mainTex"]);
    if (j.contains("noiseTex")) noiseTex.FromJson(j["noiseTex"]);
    if (j.contains("maskTex"))  maskTex.FromJson(j["maskTex"]);

    offset = J3(j.value("offset", json()), offset);
    rotationDeg = J3(j.value("rotation", json()), rotationDeg);
    rotSpeedDeg = J3(j.value("rotSpeed", json()), rotSpeedDeg);
    scaleStart = J3(j.value("scaleStart", json()), scaleStart);
    scaleEnd = J3(j.value("scaleEnd", json()), scaleEnd);

    tint = J4(j.value("tint", json()), tint);
    alphaStart = j.value("alphaStart", alphaStart);
    alphaEnd = j.value("alphaEnd", alphaEnd);
    intensity = j.value("intensity", intensity);
    blend = j.value("blend", blend);
    twoSided = j.value("twoSided", twoSided);

    mainTiling = J2(j.value("mainTiling", json()), mainTiling);
    mainScroll = J2(j.value("mainScroll", json()), mainScroll);
    noiseTiling = J2(j.value("noiseTiling", json()), noiseTiling);
    noiseScroll = J2(j.value("noiseScroll", json()), noiseScroll);
    distortion = j.value("distortion", distortion);

    dissolveEnabled = j.value("dissolve", dissolveEnabled);
    dissolveStart = j.value("dissolveStart", dissolveStart);
    dissolveEnd = j.value("dissolveEnd", dissolveEnd);
    dissolveEdge = j.value("dissolveEdge", dissolveEdge);
    dissolveEdgeColor = J4(j.value("dissolveEdgeColor", json()), dissolveEdgeColor);

    LoadModel();
}

std::unique_ptr<VFXEntry> VFXMeshEntry::Clone() const
{
    auto c = std::make_unique<VFXMeshEntry>();
    c->FromJson(ToJson());   // 参照解決込みで複製
    c->startTime = startTime;
    c->duration = duration;
    c->isPlaying = false;
    return c;
}