// ============================================================
// MapEditModeTerrainUI.cpp
// 戦闘の地図の編集：地形の部品（台地・高台・坂）を足す / 選んだ部品の中身（4 歩目の 1 段目）
// 中身を変える度に MapTerrainEdit::Refresh（足元の合わせ直し → 記録 → 台座 → 起伏・高さ場）を掛け、
// 見た目の建て直しはドラッグが終わってから（m_ViewDirty）
// ============================================================
#include "Scene/MapEditMode.h"
#include "Camera/FlyCamera.h"
#include "World/TerrainBuild.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kCell = GridWorld::kCellSize;
    const char* kSideNames[] = { "+X (east)", "-X (west)", "+Z (north)", "-Z (south)" };

    bool AlongX(uint8_t side) { return side < 2; }
}

// 画面の真ん中の地面へ足す。台地 = 6 x 6 マス・高さ 3m + 土の坂道 1 本、高台 = 8 x 8 マス・6m + 幅いっぱいの草の長い坂
void MapEditMode::AddPart(FlyCamera& camera, bool terrace)
{
    const Vector3 c = ScreenCenterGround(camera);
    int gx = 0, gz = 0;
    m_Grid.WorldToCell(c, gx, gz);
    const int size = terrace ? 8 : 6;
    const uint32_t g = MapTerrainEdit::AddPlateau(m_Map, gx - size / 2, gz - size / 2, size, size, terrace ? 6.0f : 3.0f);
    if (g == 0) return;

    if (terrace)
    {
        if (MapData::BlockPart* body = MapTerrainEdit::Block(m_Map, g, 0)) body->tag.kind = (uint16_t)MapData::kTerrace;
        MapTerrainEdit::AddRamp(m_Map, g, 0, 0, 0, size);
        if (MapData::RampPart* r = MapTerrainEdit::Ramp(m_Map, g, 0))
        {
            // 草の長い坂：18°（滑り込みで加速する）。色は床の明るい草
            const MapData::BlockPart* body = MapTerrainEdit::Block(m_Map, g, 0);
            const float rise = body ? body->top - body->base : 6.0f;
            r->tag.kind = (uint16_t)MapData::kTerrace;
            r->grassy = true;
            r->topColor = TerrainBuild::PaletteFor((TerrainGenerator::Biome)m_Map.biome).groundLight;
            r->w = (std::max)(1, (int)std::ceil(rise / (kCell * std::tan(DirectX::XMConvertToRadians(18.0f)))));
        }
        // 札の種類を揃えたので、記録・台座も作り直す
        MapTerrainEdit::Refresh(m_Map, g);
    }
    else
    {
        MapTerrainEdit::AddRamp(m_Map, g, 0, 0, 2, 2);
    }
    m_Sel = { SelType::Group, g, -1 };
    m_Dirty = true;
    m_ViewDirty = true;
}

void MapEditMode::DrawPartInspector()
{
    const uint32_t g = m_Sel.group;
    const int blocks = MapTerrainEdit::BlockCount(m_Map, g);
    const int ramps = MapTerrainEdit::RampCount(m_Map, g);
    MapData::BlockPart* body = MapTerrainEdit::Block(m_Map, g, 0);
    if (!body)
    {
        DrawZoneRampInspector();   // 山頂の長い坂・洞窟の下り坂（箱が無い）
        return;
    }
    ImGui::Text("%s  (%d box, %d ramp)", body->tag.kind == MapData::kTerrace ? "Terrace" : "Plateau", blocks, ramps);
    ImGui::TextDisabled("Move with the gizmo (2 m steps). It re-seats on the ground.");

    bool changed = false;
    int deleteRamp = -1;

    // ---- 箱（本体・2 段目）----
    for (int i = 0; i < blocks; ++i)
    {
        MapData::BlockPart* b = MapTerrainEdit::Block(m_Map, g, i);
        ImGui::PushID(i);
        ImGui::SeparatorText(i == 0 ? "Body" : "Upper tier");
        int size[2] = { b->w, b->d };
        if (ImGui::DragInt2("Size (cells, 2 m)", size, 0.1f, 1, 40))
        {
            b->w = std::clamp(size[0], 1, m_Map.gw - 4 - b->x + 2);
            b->d = std::clamp(size[1], 1, m_Map.gd - 4 - b->z + 2);
            changed = true;
        }
        float height = b->top - b->base;
        if (ImGui::DragFloat("Height (m)", &height, 0.05f, 0.5f, 30.0f, "%.2f"))
        {
            const float delta = (std::max)(height, 0.5f) - (b->top - b->base);
            b->top += delta;
            // 上に載っている段も一緒に上げ下げする
            if (i == 0)
                for (int k = 1; k < blocks; ++k)
                {
                    MapData::BlockPart* u = MapTerrainEdit::Block(m_Map, g, k);
                    u->bottom += delta; u->top += delta; u->base += delta;
                }
            changed = true;
        }
        if (i > 0)
        {
            int pos[2] = { b->x, b->z };
            if (ImGui::DragInt2("Cell (x, z)", pos, 0.1f, 2, (std::max)(m_Map.gw, m_Map.gd)))
            {
                b->x = pos[0]; b->z = pos[1];
                changed = true;
            }
        }
        if (i == 0 && ImGui::Checkbox("Walkable top", &b->raise)) changed = true;
        if (i == 0) ImGui::SetItemTooltip("Off = a solid block nobody can stand on (cells blocked for mobs)");
        ImGui::PopID();
    }

    // ---- 坂 ----
    for (int i = 0; i < ramps; ++i)
    {
        MapData::RampPart* r = MapTerrainEdit::Ramp(m_Map, g, i);
        ImGui::PushID(100 + i);
        ImGui::SeparatorText(r->grassy ? "Slope (grass)" : "Ramp (dirt path)");
        int side = r->side;
        if (ImGui::Combo("Goes down toward", &side, kSideNames, 4))
        {
            // 向きを変えたら、幅と長さを入れ替えて同じ幅・同じ長さの坂にする
            if (AlongX(r->side) != AlongX((uint8_t)side)) std::swap(r->w, r->d);
            r->side = (uint8_t)side;
            changed = true;
        }
        int& width = AlongX(r->side) ? r->d : r->w;
        int& length = AlongX(r->side) ? r->w : r->d;
        changed |= ImGui::DragInt("Offset along the edge", &r->offset, 0.1f, 0, 40);
        changed |= ImGui::DragInt("Width (cells)", &width, 0.1f, 1, 40);
        changed |= ImGui::DragInt("Length (cells)", &length, 0.1f, 1, 60);
        width = (std::max)(width, 1);
        length = (std::max)(length, 1);
        const float slope = DirectX::XMConvertToDegrees(std::atan2(r->top - r->base, length * kCell));
        // 雑魚は 40°、フローフィールドは 1 マス 1.5m（≒ 37°）まで
        ImGui::TextColored(slope > 35.0f ? ImVec4(1.0f, 0.4f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "Slope %.0f deg%s", slope, slope > 35.0f ? "  (too steep for mobs: make it longer)" : "");
        if (ImGui::Checkbox("Grass slope", &r->grassy))
        {
            const auto& P = TerrainBuild::PaletteFor((TerrainGenerator::Biome)m_Map.biome);
            r->topColor = r->grassy ? P.groundLight : P.rampTop;
            changed = true;
        }
        if (ImGui::Button("Remove This Ramp")) deleteRamp = i;
        ImGui::PopID();
    }

    // 坂を消す：group の i 番目
    if (deleteRamp >= 0)
    {
        int n = deleteRamp;
        for (size_t k = 0; k < m_Map.rampParts.size(); ++k)
            if (m_Map.rampParts[k].tag.group == g && n-- == 0)
            {
                m_Map.rampParts.erase(m_Map.rampParts.begin() + k);
                break;
            }
        changed = true;
    }

    // ---- 坂道を足す（本体へ）----
    static int s_NewSide = 0;
    ImGui::SeparatorText("Add a ramp to the body");
    ImGui::Combo("Side", &s_NewSide, kSideNames, 4);
    if (ImGui::Button("Add Ramp"))
    {
        MapTerrainEdit::AddRamp(m_Map, g, 0, s_NewSide, 0, 2);   // 幅 2 マス。長さは高さと既定の角度から
        m_Dirty = true;
        m_ViewDirty = true;
    }
    if (ramps == 0 && body->raise)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "No ramp: nobody can climb up here.");

    if (changed)
    {
        // 作り直し（Refresh）は数値のドラッグを離した時にまとめて（FlushPending）
        if (m_PartDirty != 0 && m_PartDirty != g) FlushPending();
        m_PartDirty = g;
        m_Dirty = true;
        m_ViewDirty = true;
    }
}

// ============================================================
// 山頂の長い坂 / 洞窟の下り坂（区域の縁に付く、箱の無い坂。2 段目）
// ============================================================
// 画面の真ん中の地面へ足す。向きは +X へ下る既定（窓で変える）。長さは高さと角度から：
// 山頂 = 28°（16m なら 15 マス）、洞窟 = 31°（10m なら 9 マス）
void MapEditMode::AddZoneRamp(FlyCamera& camera, bool summit)
{
    const Vector3 c = ScreenCenterGround(camera);
    int gx = 0, gz = 0;
    m_Grid.WorldToCell(c, gx, gz);
    const float rise = summit ? m_Map.summitH : m_Map.mineD;
    const float deg = summit ? 28.0f : 31.0f;
    const int length = (std::max)(1, (int)std::ceil(rise / (kCell * std::tan(DirectX::XMConvertToRadians(deg)))));
    const uint32_t g = MapTerrainEdit::AddZoneRamp(m_Map, summit, gx, gz - 2, 0, 4, length);
    if (g == 0) return;
    m_Sel = { SelType::Group, g, -1 };
    m_Dirty = true;
    m_ViewDirty = true;
}

void MapEditMode::DrawZoneRampInspector()
{
    const uint32_t g = m_Sel.group;
    MapData::RampPart* r = MapTerrainEdit::Ramp(m_Map, g, 0);
    if (!r) return;
    const bool summit = r->tag.kind == MapData::kSummitRamp;
    ImGui::Text("%s", summit ? "Summit ramp (grass slope to the plain)" : "Mine ramp (down to the mine floor)");
    ImGui::TextWrapped(summit
        ? "Move it so the high end touches the summit edge. The foot is flattened to the ground."
        : "Keep it inside the pit with the high end at the pit edge: the cell row beyond the high end becomes the cave mouth.");

    bool changed = false;
    int pos[2] = { r->x, r->z };
    if (ImGui::DragInt2("Cell (x, z)", pos, 0.1f, 2, (std::max)(m_Map.gw, m_Map.gd) - 3))
    {
        r->x = std::clamp(pos[0], 2, m_Map.gw - 2 - r->w);
        r->z = std::clamp(pos[1], 2, m_Map.gd - 2 - r->d);
        changed = true;
    }
    int side = r->side;
    if (ImGui::Combo("Goes down toward", &side, kSideNames, 4))
    {
        if (AlongX(r->side) != AlongX((uint8_t)side)) std::swap(r->w, r->d);
        r->side = (uint8_t)side;
        changed = true;
    }
    int& width = AlongX(r->side) ? r->d : r->w;
    int& length = AlongX(r->side) ? r->w : r->d;
    changed |= ImGui::DragInt("Width (cells)", &width, 0.1f, 1, 20);
    changed |= ImGui::DragInt("Length (cells)", &length, 0.1f, 1, 60);
    width = (std::max)(width, 1);
    length = (std::max)(length, 1);
    const float slope = DirectX::XMConvertToDegrees(std::atan2(r->top - r->base, length * kCell));
    ImGui::TextColored(slope > 35.0f ? ImVec4(1.0f, 0.4f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
        "Slope %.0f deg%s", slope, slope > 35.0f ? "  (too steep for mobs: make it longer)" : "");

    if (changed)
    {
        // 離した時に Refresh（洞窟の坂なら洞の口・岩の壁・屋根も作り直す）
        if (m_PartDirty != 0 && m_PartDirty != g) FlushPending();
        m_PartDirty = g;
        m_Dirty = true;
        m_ViewDirty = true;
    }
}

void MapEditMode::TestFocusAt(FlyCamera& camera, const Vector3& pos, float radius)
{
    camera.Focus(pos, radius);
}
