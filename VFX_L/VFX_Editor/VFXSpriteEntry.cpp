// ============================================================
// VFXSpriteEntry.cpp
// ============================================================
#include "VFX_Editor/VFXSpriteEntry.h"
#include "VFX_Editor/VFXSpriteRenderer.h"
#include "VFX_Editor/VFXFileList.h"
#include "Graphics/Material/Texture.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr const char* kSheetDir = "Assets/VFX/SpriteSheet";

    json V3(const Vector3& v) { return { v.x, v.y, v.z }; }
    json V4(const Vector4& v) { return { v.x, v.y, v.z, v.w }; }
    Vector3 J3(const json& j, const Vector3& d) { return j.is_array() && j.size() >= 3 ? Vector3(j[0], j[1], j[2]) : d; }
    Vector4 J4(const json& j, const Vector4& d) { return j.is_array() && j.size() >= 4 ? Vector4(j[0], j[1], j[2], j[3]) : d; }
}

// ============================================================
// 解釈（CPU の描画と GPU の表で同じ式を使う）
// ============================================================
bool VFXSpriteEntry::IsLooping() const
{
    if (loopMode == (int)LoopMode::Loop) return true;
    if (loopMode == (int)LoopMode::Once) return false;
    const auto* s = GetSheet();
    return s && s->loop;
}

float VFXSpriteEntry::FrameTime() const
{
    const auto* s = GetSheet();
    const float ft = s ? s->frameTime : 0.05f;
    return ft / (std::max)(speed, 0.01f);
}

Vector2 VFXSpriteEntry::Pivot() const
{
    switch ((Anchor)anchor)
    {
    case Anchor::Center: return Vector2(0.5f, 0.5f);
    case Anchor::Bottom: return Vector2(0.5f, 1.0f);
    default:
    {
        const auto* s = GetSheet();
        return s ? s->pivot : Vector2(0.5f, 0.5f);
    }
    }
}

Vector2 VFXSpriteEntry::WorldSize() const
{
    const auto* s = GetSheet();
    const float aspect = (s && s->cellH > 0) ? (float)s->cellW / (float)s->cellH : 1.0f;
    return Vector2(size * aspect, size);
}

int VFXSpriteEntry::CurrentFrame() const
{
    const auto* s = GetSheet();
    if (!s) return -1;
    const int f = (int)std::floor(m_Age / FrameTime());
    if (IsLooping()) return f % (std::max)(1, s->frameCount);
    return (f < s->frameCount) ? f : -1;
}

// ============================================================
// 再生
// ============================================================
void VFXSpriteEntry::OnPlay(const VFXContext&)
{
    isPlaying = true;
    m_Age = 0.0f;
}

void VFXSpriteEntry::OnStop(const VFXContext&)
{
    isPlaying = false;
}

void VFXSpriteEntry::OnUpdate(float dt, const VFXContext&)
{
    m_Age += dt;
}

void VFXSpriteEntry::Submit(VFXSpriteRenderer& renderer, const Vector3& worldOffset)
{
    if (!isPlaying) return;
    const auto* s = GetSheet();
    const int frame = CurrentFrame();
    if (!s || !s->texture || frame < 0) return;

    VFXSpriteDrawItem item;
    item.texture = s->texture.get();
    item.point = s->point;
    item.quad.position = worldOffset + offset;
    item.quad.rotation = DirectX::XMConvertToRadians(rotationDeg);
    item.quad.size = WorldSize();
    item.quad.pivot = Pivot();
    item.quad.uvRect = s->FrameUV(frame);
    item.quad.color = color;
    item.quad.facing = (uint32_t)std::clamp(facing, 0, 2);
    item.quad.flags = (blend == 1) ? 1u : 0u;
    renderer.Submit(item);
}

// ============================================================
// ImGui
// ============================================================
void VFXSpriteEntry::OnImGui()
{
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Sprite Sheet");
    const bool changed = VFXFileList::Combo("Sheet", kSheetDir, { ".png" }, sheetPath);

    const auto* s = GetSheet();
    if (!sheetPath.empty() && !s)
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "load failed");
    if (s)
    {
        ImGui::TextDisabled("%d frames, %.0f fps, %s, cell %dx%d",
            s->frameCount, 1.0f / (std::max)(s->frameTime, 0.001f),
            s->loop ? "loop" : "once", s->cellW, s->cellH);

        // 今のコマ（止まっている時は 0 コマ目）
        int frame = isPlaying ? CurrentFrame() : 0;
        if (frame < 0) frame = s->frameCount - 1;
        const Vector4 uv = s->FrameUV(frame);
        ImGui::Image((ImTextureID)s->texture->GetSRV(), ImVec2(96, 96),
            ImVec2(uv.x, uv.y), ImVec2(uv.x + uv.z, uv.y + uv.w),
            ImVec4(1, 1, 1, 1), ImVec4(0.4f, 0.4f, 0.4f, 1));
        ImGui::SameLine();
        ImGui::TextDisabled("frame %d", frame);

        // 選び直した時は 1 回分の長さに合わせる（繰り返す物はそのまま）
        if (changed && !IsLooping())
            duration = s->frameTime * s->frameCount / (std::max)(speed, 0.01f);
        if (ImGui::Button("Fit Duration"))
            duration = IsLooping() ? -1.0f : FrameTime() * s->frameCount;
        ImGui::SameLine();
        ImGui::TextDisabled(IsLooping() ? "(loop: -1 = until the effect stops)" : "(one play)");
    }
    ImGui::Separator();

    ImGui::DragFloat3("Offset", &offset.x, 0.05f);
    ImGui::DragFloat("Size (m)", &size, 0.05f, 0.05f, 50.0f);
    ImGui::TextDisabled("height of one frame; width follows the cell aspect");
    const char* facingNames[] = { "Billboard (face camera)", "Upright (turn around Y)", "Ground (flat)" };
    ImGui::Combo("Facing", &facing, facingNames, IM_ARRAYSIZE(facingNames));
    ImGui::DragFloat("Rotation (deg)", &rotationDeg, 1.0f, -360.0f, 360.0f);
    const char* anchorNames[] = { "Sheet pivot", "Center", "Bottom" };
    ImGui::Combo("Anchor", &anchor, anchorNames, IM_ARRAYSIZE(anchorNames));
    ImGui::Separator();

    ImGui::ColorEdit4("Color", &color.x);
    const char* blendNames[] = { "Alpha", "Additive (glow)" };
    ImGui::Combo("Blend", &blend, blendNames, IM_ARRAYSIZE(blendNames));
    ImGui::DragFloat("Speed", &speed, 0.01f, 0.05f, 10.0f);
    const char* loopNames[] = { "From sheet", "Once", "Loop" };
    ImGui::Combo("Loop", &loopMode, loopNames, IM_ARRAYSIZE(loopNames));
}

// ============================================================
// 複製・保存
// ============================================================
std::unique_ptr<VFXEntry> VFXSpriteEntry::Clone() const
{
    auto e = std::make_unique<VFXSpriteEntry>(*this);
    e->isPlaying = false;
    e->m_Age = 0.0f;
    return e;
}

json VFXSpriteEntry::ToJson() const
{
    return {
        { "sheet", sheetPath },
        { "offset", V3(offset) },
        { "size", size },
        { "facing", facing },
        { "rotation", rotationDeg },
        { "color", V4(color) },
        { "blend", blend },
        { "speed", speed },
        { "loopMode", loopMode },
        { "anchor", anchor },
    };
}

void VFXSpriteEntry::FromJson(const json& j)
{
    sheetPath = j.value("sheet", std::string());
    offset = J3(j.value("offset", json()), offset);
    size = j.value("size", size);
    facing = j.value("facing", facing);
    rotationDeg = j.value("rotation", rotationDeg);
    color = J4(j.value("color", json()), color);
    blend = j.value("blend", blend);
    speed = j.value("speed", speed);
    loopMode = j.value("loopMode", loopMode);
    anchor = j.value("anchor", anchor);
}
