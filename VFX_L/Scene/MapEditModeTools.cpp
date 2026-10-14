// ============================================================
// MapEditModeTools.cpp
// 戦闘の地図の編集：地面に直接塗る道具（区域の筆・起伏の筆）と、その窓の欄。
// どちらも左ボタンを押している間はデータだけ書き換え、離した時に作り直す
// （起伏・高さ場の作り直しと床のメッシュの建て直しは毎フレームやるには重い）
// ============================================================
#include "Scene/MapEditMode.h"
#include "Debug/DebugManager.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    // 地面に沿った輪（筆の範囲の目安）
    void GroundCircle(const GridWorld& grid, const Vector3& c, float r, const Color& col)
    {
        auto& dbg = DebugManager::Get();
        constexpr int kSeg = 40;
        for (int i = 0; i < kSeg; ++i)
        {
            const float a0 = i * 6.2831853f / kSeg, a1 = (i + 1) * 6.2831853f / kSeg;
            const Vector3 p0(c.x + std::cos(a0) * r, 0.0f, c.z + std::sin(a0) * r), p1(c.x + std::cos(a1) * r, 0.0f, c.z + std::sin(a1) * r);
            dbg.AddDebugLine({ p0.x, grid.SampleHeight(p0.x, p0.z) + 0.3f, p0.z },
                { p1.x, grid.SampleHeight(p1.x, p1.z) + 0.3f, p1.z }, col);
        }
    }
}

// 塗る道具が有効なら true（その間、左クリックは物を選ばない）
bool MapEditMode::UpdateTools(bool mouseFree, bool placing)
{
    DrawHillMarks();
    const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool zoneTool = m_PaintZone >= 0 && !placing && MapTerrainEdit::CanEditZones(m_Map);
    const bool brushTool = !zoneTool && m_BrushMode >= 0 && !placing && MapTerrainEdit::HasParts(m_Map) && m_Map.relief;

    Vector3 hit;
    const bool onGround = (zoneTool || brushTool) && mouseFree && RayGround(hit);

    // ---- 区域の筆（山頂 / 平原 / 洞窟）：マス単位 ----
    if (zoneTool && onGround)
    {
        int gx = 0, gz = 0;
        m_Grid.WorldToCell(hit, gx, gz);
        const Color col = m_PaintZone == 1 ? Color(1.0f, 0.9f, 0.4f, 1.0f) : m_PaintZone == 2 ? Color(0.6f, 0.5f, 1.0f, 1.0f)
            : Color(0.4f, 1.0f, 0.5f, 1.0f);
        GroundCircle(m_Grid, m_Grid.CellToWorld(gx, gz), ((float)m_PaintRadius + 0.5f) * GridWorld::kCellSize, col);
        if (lmb && MapTerrainEdit::PaintZone(m_Map, gx, gz, m_PaintRadius, (uint8_t)m_PaintZone) > 0)
            m_ZoneDirty = true;
    }
    if (m_ZoneDirty && !lmb)
    {
        m_ZoneDirty = false;
        MapTerrainEdit::RegenZones(m_Map);
        m_Dirty = true;
        m_ViewDirty = true;
    }

    // ---- 起伏の筆（上げる / 下げる / 均す / 平らにする）：押している間、毎フレーム少しずつ ----
    if (brushTool && onGround)
    {
        const Color col = m_BrushMode == 0 ? Color(1.0f, 0.6f, 0.3f, 1.0f) : m_BrushMode == 1 ? Color(0.4f, 0.7f, 1.0f, 1.0f)
            : Color(0.9f, 0.9f, 0.9f, 1.0f);
        GroundCircle(m_Grid, hit, m_BrushRadius, col);
        GroundCircle(m_Grid, hit, m_BrushRadius * 0.5f, col * 0.6f);   // 半分の強さになる辺り

        if (lmb)
        {
            const float dt = (std::min)(ImGui::GetIO().DeltaTime, 0.1f);
            // 平らにする：押し始めの所の高さへ揃える
            if (!m_BrushStroke) m_BrushFlattenY = m_Grid.SampleHeight(hit.x, hit.z);
            m_BrushStroke = true;
            const auto mode = (MapTerrainEdit::BrushMode)m_BrushMode;
            // 上げ下げは m/秒、均し・平らは 1 秒でほぼ寄り切る割合
            const float amount = (m_BrushMode <= 1) ? m_BrushStrength * dt : (std::min)(1.0f, m_BrushStrength * dt);
            if (MapTerrainEdit::BrushRelief(m_Map, hit.x, hit.z, m_BrushRadius, mode, amount, m_BrushFlattenY) > 0)
                m_BrushDirty = true;
        }
    }
    if (!lmb)
    {
        m_BrushStroke = false;
        if (m_BrushDirty)
        {
            m_BrushDirty = false;
            MapTerrainEdit::FinishBrush(m_Map);
            m_Dirty = true;
            m_ViewDirty = true;
        }
    }
    return zoneTool || brushTool;
}

// 窓の欄：区域の筆と山頂 / 洞窟の坂、起伏の筆
void MapEditMode::DrawToolsUI(FlyCamera& camera)
{
    // ---- 区域（平原 / 山頂 / 洞窟）を塗る ----
    ImGui::SeparatorText("Zones (paint summit / plain / mine)");
    if (!MapTerrainEdit::CanEditZones(m_Map))
        ImGui::TextDisabled("This map was saved before zone editing existed: generate it again.");
    else
    {
        const char* modes[] = { "Off (select)", "Plain", "Summit (+16 m)", "Mine (-10 m, cave)" };
        int mode = m_PaintZone + 1;
        if (ImGui::Combo("Paint", &mode, modes, 4))
        {
            m_PaintZone = mode - 1;
            if (m_PaintZone >= 0) m_BrushMode = -1;   // 道具は 1 つだけ
        }
        ImGui::SliderInt("Brush radius (cells)", &m_PaintRadius, 0, 8);
        ImGui::TextDisabled("Hold LMB and drag on the ground. Cliffs, cave walls, roof");
        ImGui::TextDisabled("rocks and torches are rebuilt when you release.");
        if (ImGui::Button("Add Summit Ramp")) AddZoneRamp(camera, true);
        ImGui::SetItemTooltip("Grass slope from the summit down to the plain. Put its high end on the summit edge");
        ImGui::SameLine();
        if (ImGui::Button("Add Mine Ramp")) AddZoneRamp(camera, false);
        ImGui::SetItemTooltip("Dirt ramp inside the pit, down to the mine floor. Its high end is the cave mouth");
    }

    // ---- 起伏の筆 ----
    ImGui::SeparatorText("Hills (sculpt gentle ground)");
    if (!MapTerrainEdit::HasParts(m_Map) || !m_Map.relief)
        ImGui::TextDisabled("This map has no editable relief (old file or relief off).");
    else
    {
        const char* modes[] = { "Off (select)", "Raise", "Lower", "Smooth", "Flatten (to where you start)" };
        int mode = m_BrushMode + 1;
        if (ImGui::Combo("Sculpt", &mode, modes, 5))
        {
            m_BrushMode = mode - 1;
            if (m_BrushMode >= 0) m_PaintZone = -1;
        }
        // "##brush"：丘の部品の欄にも "Radius (m)" があり、同じ窓で ID が被る（ImGui の警告）
        ImGui::SliderFloat("Radius (m)##brush", &m_BrushRadius, 2.0f, 30.0f, "%.0f");
        ImGui::SliderFloat("Strength##brush", &m_BrushStrength, 0.5f, 10.0f, "%.1f");
        ImGui::SetItemTooltip("Raise / Lower: metres per second at the centre. Smooth / Flatten: how fast it settles");
        ImGui::TextDisabled("Hold LMB on the plain or the summit. The ground is rebuilt when");
        ImGui::TextDisabled("you release. Slopes stay walkable; plateau feet stay flat.");
    }

    // ---- 起伏の中身：全体の設定と丘の部品（2026-10-05、ユーザー：丘・起伏も部品と同じ扱いに）----
    ImGui::SeparatorText("Relief (whole map) and hill parts");
    if (!MapTerrainEdit::CanEditHills(m_Map))
        ImGui::TextDisabled("This map was saved before hill editing existed: generate it again.");
    else
    {
        MapData::ReliefParams& rp = m_Map.reliefParams;
        bool changed = false;
        changed |= ImGui::SliderFloat("Hill height (m)", &rp.hillHeight, 0.0f, 10.0f, "%.1f");
        changed |= ImGui::SliderFloat("Hill size (m)", &rp.hillScale, 10.0f, 120.0f, "%.0f");
        changed |= ImGui::SliderFloat("Ripple height (m)", &rp.detailHeight, 0.0f, 4.0f, "%.1f");
        changed |= ImGui::SliderFloat("Ripple size (m)", &rp.detailScale, 4.0f, 60.0f, "%.0f");
        changed |= ImGui::SliderFloat("Summit relief x", &rp.summitMul, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Max slope (deg)", &m_Map.reliefMaxSlopeDeg, 10.0f, 38.0f, "%.0f");
        ImGui::SetItemTooltip("Mobs walk up to 40 deg. Diagonals get up to 1.41x this");
        if (changed) ApplyReliefChange();

        ImGui::Text("Hill parts: %zu", m_Map.hills.size());
        ImGui::SameLine();
        ImGui::Checkbox("Show##hills", &m_ShowHills);
        if (ImGui::Button("Add Hill"))
        {
            const Vector3 c = ScreenCenterGround(camera);
            m_ShowHills = true;
            m_Sel = { SelType::Group, MapTerrainEdit::AddHill(m_Map, c.x, c.z, 6.0f, 2.0f), -1 };
            MapTerrainEdit::Rederive(m_Map);
            m_Dirty = true;
            m_ViewDirty = m_ReseatDirty = true;
        }
        ImGui::SetItemTooltip("A round mound (or a hollow with negative height). Click inside its ring to select it");
        ImGui::SameLine();
        if (ImGui::Button("Remove All Hills"))
        {
            m_Map.hills.clear();
            m_Sel = {};
            ApplyReliefChange();
        }
    }
}

// ============================================================
// 丘の部品（起伏に足す 1 個の盛り上がり / 窪み）
// ============================================================
// 起伏の設定・丘の部品を変えた後：素の起伏 → 起伏・高さ場を作り直す。
// 台地などの足元の合わせ直しと床のメッシュの建て直しは、ドラッグが終わってから（MapEditMode::Update）
void MapEditMode::ApplyReliefChange()
{
    // 印だけ付ける：素の起伏 → 起伏・高さ場の作り直しは、スライダー / ギズモを離した時に FlushPending がまとめて
    m_Dirty = true;
    m_ReliefDirty = m_ViewDirty = true;
}

// 丘の部品を輪で（選んだ物は黄、盛り上がりは緑、窪みは青）。輪を見せている時だけ選べる
void MapEditMode::DrawHillMarks()
{
    if (!m_Loaded || !m_ShowHills || !MapTerrainEdit::CanEditHills(m_Map)) return;
    for (const auto& h : m_Map.hills)
    {
        const bool selected = m_Sel.type == SelType::Group && m_Sel.group == h.tag.group;
        const Color col = selected ? Color(1.0f, 0.85f, 0.2f, 1.0f)
            : h.height >= 0.0f ? Color(0.35f, 0.75f, 0.35f, 1.0f) : Color(0.35f, 0.55f, 0.9f, 1.0f);
        GroundCircle(m_Grid, { h.x, 0.0f, h.z }, h.radius, col);
        if (selected) GroundCircle(m_Grid, { h.x, 0.0f, h.z }, h.radius * 0.5f, col);
    }
}

void MapEditMode::DrawHillInspector()
{
    const int hi = MapTerrainEdit::FindHill(m_Map, m_Sel.group);
    if (hi < 0) return;
    MapData::Hill& h = m_Map.hills[(size_t)hi];
    ImGui::Text("Hill part");
    ImGui::TextDisabled("Move with the gizmo. The ground is rebuilt when you release.");
    bool changed = false;
    float pos[2] = { h.x, h.z };
    if (ImGui::DragFloat2("Position (x, z)##hill", pos, 0.1f)) { h.x = pos[0]; h.z = pos[1]; changed = true; }
    changed |= ImGui::SliderFloat("Radius (m)##hill", &h.radius, 1.0f, 40.0f, "%.1f");
    changed |= ImGui::SliderFloat("Height (m)##hill", &h.height, -6.0f, 10.0f, "%.1f");
    ImGui::SetItemTooltip("Negative = a hollow. Steep sides are flattened to the max slope");
    // 一番急な所の目安：1.54 × 高さ / 半径
    const float steep = DirectX::XMConvertToDegrees(std::atan(1.54f * std::fabs(h.height) / (std::max)(h.radius, 0.1f)));
    ImGui::TextColored(steep > m_Map.reliefMaxSlopeDeg ? ImVec4(1.0f, 0.7f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
        "Steepest side about %.0f deg%s", steep, steep > m_Map.reliefMaxSlopeDeg ? "  (will be flattened to the limit)" : "");
    if (changed) ApplyReliefChange();
}
