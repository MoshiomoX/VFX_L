// ============================================================
// VFXTrailEntry.cpp
// ============================================================
#include "VFX_Editor/VFXTrailEntry.h"
#include "Particle/GPUParticleSystem.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    json V3(const Vector3& v) { return { v.x, v.y, v.z }; }
    json V4(const Vector4& v) { return { v.x, v.y, v.z, v.w }; }
    Vector3 J3(const json& j, const Vector3& d) { return j.is_array() && j.size() >= 3 ? Vector3(j[0], j[1], j[2]) : d; }
    Vector4 J4(const json& j, const Vector4& d) { return j.is_array() && j.size() >= 4 ? Vector4(j[0], j[1], j[2], j[3]) : d; }
}

VFXTrailEntry::~VFXTrailEntry()
{
    DetachTrail();     // 帯は自分の分の style 参照を持っているので、先に切り離しても残って消えていく
    ReleaseStyle();
}

// ============================================
// style：登録は再生の最初、以降は毎フレーム更新。
//   OnStop では解除しない（次の再生でそのまま使う。切り離した帯は自分で参照を持つ）
// ============================================
void VFXTrailEntry::SyncStyle(const VFXContext& ctx)
{
    if (!ctx.particleSystem) return;

    style.texture = tex.texture;

    if (m_StyleId < 0)
    {
        m_StyleId = ctx.particleSystem->RegisterTrailStyle(style);
        m_Owner = (m_StyleId >= 0) ? ctx.particleSystem : nullptr;
    }
    else
    {
        ctx.particleSystem->UpdateTrailStyle(m_StyleId, style);
    }
}

void VFXTrailEntry::ReleaseStyle()
{
    if (m_StyleId >= 0 && m_Owner)
        m_Owner->UnregisterTrailStyle(m_StyleId);
    m_StyleId = -1;
    m_Owner = nullptr;
}

void VFXTrailEntry::DetachTrail()
{
    if (m_TrailId >= 0 && m_Owner)
        m_Owner->ReleaseEffectTrail(m_TrailId);
    m_TrailId = -1;
}

// 帯そのものは Submit で位置が分かってから作る。
// ループの頭（ResetTimeline は OnStop を呼ばない）で前の帯が残っていたら切り離す：
// startTime > 0 だと再生されない間にエフェクトが動いていて、繋ぐと直線が引かれるため
void VFXTrailEntry::OnPlay(const VFXContext& ctx)
{
    isPlaying = true;
    DetachTrail();
    SyncStyle(ctx);
}

void VFXTrailEntry::OnStop(const VFXContext&)
{
    isPlaying = false;
    DetachTrail();
}

void VFXTrailEntry::OnUpdate(float, const VFXContext& ctx)
{
    SyncStyle(ctx);
}

void VFXTrailEntry::Submit(const Vector3& worldOffset, bool jumped)
{
    if (!isPlaying || !m_Owner || m_StyleId < 0) return;

    const Vector3 head = worldOffset + offset;

    // 瞬間移動の前後を 1 本の帯で繋がない：前の帯はその場に残して消えていく
    if (jumped) DetachTrail();

    if (m_TrailId < 0)
        m_TrailId = m_Owner->CreateEffectTrail(m_StyleId, head, minDistance);
    else
        m_Owner->MoveEffectTrail(m_TrailId, head, minDistance);
}

void VFXTrailEntry::OnImGui()
{
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Trail (ribbon behind the effect position)");
    if (m_StyleId < 0)
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "not registered yet (press Play) or no free style slot");
    else if (isPlaying && m_TrailId < 0)
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "no free trail slot");

    ImGui::DragFloat3("Offset", &offset.x, 0.05f);
    ImGui::DragFloat("Min Distance", &minDistance, 0.01f, 0.01f, 5.0f);
    ImGui::DragFloat("Length (sec)", &style.lifetime, 0.01f, 0.02f, 10.0f);
    ImGui::TextDisabled("max length %.1f m (%u points x Min Distance)",
        minDistance * (float)kEffectTrailPoints, kEffectTrailPoints);

    ImGui::DragFloat("Width Head", &style.widthHead, 0.005f, 0.0f, 20.0f);
    ImGui::DragFloat("Width Tail", &style.widthTail, 0.005f, 0.0f, 20.0f);
    ImGui::ColorEdit4("Color Head", &style.colorHead.x);
    ImGui::ColorEdit4("Color Tail", &style.colorTail.x);
    ImGui::DragFloat("Intensity", &style.intensity, 0.05f, 0.0f, 50.0f);
    ImGui::SliderFloat("Soft Edge", &style.softEdge, 0.0f, 1.0f);
    const char* blendNames[] = { "Additive", "Alpha" };
    ImGui::Combo("Blend", &style.blend, blendNames, 2);

    tex.OnImGui("Trail Texture", "Assets/VFX/Tex");
    int uvMode = style.uvTile ? 1 : 0;
    const char* uvModeNames[] = { "Stretch", "Tile (world)" };
    if (ImGui::Combo("UV Mode", &uvMode, uvModeNames, 2))
        style.uvTile = (uvMode == 1);
    ImGui::DragFloat(style.uvTile ? "Tiles per Meter" : "UV Repeat", &style.uvRepeat, 0.05f, 0.01f, 50.0f);
    ImGui::DragFloat("UV Scroll", &style.uvScroll, 0.01f, -20.0f, 20.0f);

    ImGui::TextDisabled("the effect itself has to move (Projectile Follow Test)");
}

std::unique_ptr<VFXEntry> VFXTrailEntry::Clone() const
{
    // 設定だけ写す。style と帯の登録はインスタンスごと（複製先が再生時に自分で登録する）
    auto c = std::make_unique<VFXTrailEntry>();
    c->startTime = startTime;
    c->duration = duration;
    c->isPlaying = false;
    c->offset = offset;
    c->minDistance = minDistance;
    c->style = style;
    c->tex = tex;
    return c;
}

json VFXTrailEntry::ToJson() const
{
    return {
        { "offset", V3(offset) },
        { "minDistance", minDistance },
        { "lifetime", style.lifetime },
        { "widthHead", style.widthHead },
        { "widthTail", style.widthTail },
        { "colorHead", V4(style.colorHead) },
        { "colorTail", V4(style.colorTail) },
        { "intensity", style.intensity },
        { "softEdge", style.softEdge },
        { "blend", style.blend },
        { "uvMode", style.uvTile ? 1 : 0 },
        { "uvRepeat", style.uvRepeat },
        { "uvScroll", style.uvScroll },
        { "tex", tex.ToJson() },
    };
}

void VFXTrailEntry::FromJson(const json& j)
{
    offset = J3(j.value("offset", json()), offset);
    minDistance = j.value("minDistance", minDistance);
    style.lifetime = j.value("lifetime", style.lifetime);
    style.widthHead = j.value("widthHead", style.widthHead);
    style.widthTail = j.value("widthTail", style.widthTail);
    style.colorHead = J4(j.value("colorHead", json()), style.colorHead);
    style.colorTail = J4(j.value("colorTail", json()), style.colorTail);
    style.intensity = j.value("intensity", style.intensity);
    style.softEdge = j.value("softEdge", style.softEdge);
    style.blend = j.value("blend", style.blend);
    style.uvTile = (j.value("uvMode", 0) == 1);
    style.uvRepeat = j.value("uvRepeat", style.uvRepeat);
    style.uvScroll = j.value("uvScroll", style.uvScroll);
    if (j.contains("tex")) tex.FromJson(j["tex"]);
}
