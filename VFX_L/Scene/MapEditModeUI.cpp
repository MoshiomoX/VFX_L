// ============================================================
// MapEditModeUI.cpp
// 戦闘の地図の編集：ImGui の窓（地図の読み書き・機能付きの置き物・選んだ物の中身）
// ============================================================
#include "Scene/MapEditMode.h"
#include "Camera/FlyCamera.h"
#include "Core/Application.h"
#include "Graphics/Model/Model.h"
#include "imgui.h"
#include <algorithm>
#include <string>

using namespace DirectX::SimpleMath;

namespace
{
    const char* KindName(uint16_t kind)
    {
        static const char* kNames[] = { "Floor", "Outer Wall", "Summit Ramp", "Mine Ramp", "Cave Roof", "Terrace", "Plateau",
            "Skirt", "Tree", "Rock", "Bush", "Edge Rock", "Ruin Wall", "Torch", "Roof Rock", "Placed (editor)" };
        return kind < std::size(kNames) ? kNames[kind] : "?";
    }
}

void MapEditMode::DrawWindow(FlyCamera& camera)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();   // 別窓が有効なので、位置は本体の窓からの相対で決める
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 370.0f, vp->Pos.y + 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 640.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Battle Map Editor");

    // ---- 地図 ----
    ImGui::SeparatorText("Map (Assets/Data/MapData/<name>.vmap)");
    ImGui::InputText("Name", m_NameBuf, sizeof(m_NameBuf));
    ImGui::BeginDisabled(!m_Loaded);
    if (ImGui::Button("Save (Ctrl+S)")) Save();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Load")) Load(m_NameBuf);
    ImGui::SameLine();
    if (ImGui::Button("Refresh List")) m_Files = MapData::List();
    if (m_Files.empty() && !m_Loaded) m_Files = MapData::List();
    if (!m_Files.empty() && ImGui::BeginListBox("##maps", ImVec2(-FLT_MIN, 4.0f * ImGui::GetTextLineHeightWithSpacing())))
    {
        for (const auto& f : m_Files)
            if (ImGui::Selectable(f.c_str(), f == m_NameBuf))
                strcpy_s(m_NameBuf, f.c_str());
        ImGui::EndListBox();
    }

    ImGui::SeparatorText("New map from seed");
    ImGui::InputInt("Seed", &m_GenSeed);
    const char* biomes[] = { "Grassland (stage 1)", "Desert (stage 2)", "Ruins (stage 3)" };
    ImGui::Combo("Biome", &m_GenBiome, biomes, 3);
    if (ImGui::Button("Generate"))
        Generate((uint32_t)(std::max)(m_GenSeed, 0), m_GenBiome);
    ImGui::SetItemTooltip("Generates the same terrain the battle makes for this seed, then you edit and save it");

    if (!m_Status.empty()) ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", m_Status.c_str());
    if (!m_Loaded)
    {
        ImGui::TextWrapped("Load a saved map or generate one from a seed to start.");
        ImGui::End();
        return;
    }

    ImGui::Text("%s%s   seed %u   %zu props", m_NameBuf, m_Dirty ? " *" : "", m_Map.seed, m_Map.props.size());
    if (ImGui::Button("Save and play (F1 battle)") && Save())
    {
        // 次に始まる戦闘シーンがこの地図を読む（戦闘の Terrain パネルの Regenerate で外れる）
        MapData::PlayOverride() = m_NameBuf;
        Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::COLLISION_TEST);
    }

    ImGui::Checkbox("Follow ground when moved", &m_GroundFollow);
    ImGui::Checkbox("Show collision (green) / blocked cells (red)", &m_ShowCollision);

    // ---- 機能付きの置き物 ----
    ImGui::SeparatorText("Prefabs (work in battle as they are)");
    int crates = 0, gates = 0;
    for (const auto& p : m_Map.placements) (p.type == MapData::kPlaceBossGate ? gates : crates)++;
    ImGui::Text("Reward crates: %d   Boss gate: %d", crates, gates);
    if (ImGui::Button("Add Reward Crate"))
        m_Sel = { SelType::Placement, 0, AddPlacement(MapData::kPlaceCrate, ScreenCenterGround(camera)) };
    ImGui::SameLine();
    if (ImGui::Button(gates ? "Move Boss Gate Here" : "Add Boss Gate"))
        m_Sel = { SelType::Placement, 0, AddPlacement(MapData::kPlaceBossGate, ScreenCenterGround(camera)) };
    ImGui::TextDisabled("Placed at the ground in the middle of the view.");
    if (crates == 0) ImGui::TextDisabled("No crate here -> the battle places crates by seed.");
    if (gates == 0) ImGui::TextDisabled("No gate here -> the battle places the gate by seed.");

    DrawInspector(camera);

    ImGui::SeparatorText("Keys");
    ImGui::TextDisabled("LMB select / place   Gizmo move   R / Shift+R rotate");
    ImGui::TextDisabled("Del delete   Ctrl+D duplicate   F focus   X deselect");
    ImGui::End();
}

// 選んだ物の中身
void MapEditMode::DrawInspector(FlyCamera& camera)
{
    ImGui::SeparatorText("Selected");
    Vector3 pos;
    if (!SelectionPos(pos))
    {
        m_Sel = {};
        ImGui::TextDisabled("(nothing) - click a tree, rock, crate ...");
        return;
    }

    if (m_Sel.type == SelType::Placement)
    {
        auto& p = m_Map.placements[(size_t)m_Sel.placement];
        ImGui::Text("%s", p.type == MapData::kPlaceBossGate ? "Boss Gate (F summons the boss)" : "Reward Crate (pay gold, pick 1 of 4)");
        Vector3 np = p.pos;
        if (ImGui::DragFloat3("Position", &np.x, 0.05f)) { p.pos = np; m_Dirty = true; }
        if (ImGui::DragFloat("Yaw", &p.yawDeg, 1.0f, 0.0f, 360.0f, "%.0f deg")) m_Dirty = true;
        if (ImGui::Button("Snap To Ground")) { p.pos.y = m_Grid.SampleHeight(p.pos.x, p.pos.z); m_Dirty = true; }
    }
    else
    {
        const int pi = MapEdit::FindProp(m_Map, m_Sel.group);
        const MapData::Prop cur = m_Map.props[(size_t)pi];
        const std::string& path = m_Map.models[(size_t)cur.model];
        const size_t slash = path.find_last_of('/');
        ImGui::Text("%s  (%s)", KindName(cur.tag.kind), path.substr(slash == std::string::npos ? 0 : slash + 1).c_str());

        Vector3 np = cur.pos;
        if (ImGui::DragFloat3("Position", &np.x, 0.05f))
        {
            // 数字で動かす時は地面に付けない（高さも直接決められるように）
            const bool follow = m_GroundFollow;
            m_GroundFollow = false;
            ApplyMove(np - cur.pos);
            m_GroundFollow = follow;
        }

        if (MapEdit::IsSimpleProp(m_Map, m_Sel.group))
        {
            MapEdit::Collision mode = MapEdit::CollisionOf(m_Map, m_Sel.group);
            auto& p = m_Map.props[(size_t)pi];
            bool changed = false;
            changed |= ImGui::DragFloat("Yaw", &p.yawDeg, 1.0f, 0.0f, 360.0f, "%.0f deg");
            float size = p.scale;
            if (ImGui::DragFloat("Scale", &size, 0.002f, 0.001f, 100.0f, "%.4f"))
            {
                // 底の高さを保ったまま拡縮する
                if (auto model = GetModel(path))
                    p.pos.y += model->GetBoundsMin().y * (p.scale - size);
                p.scale = size;
                changed = true;
            }
            int m = (int)mode;
            const char* modes[] = { "None (decor only)", "Trunk (tree: 0.6 m post)", "Footprint (rock: whole base)" };
            if (ImGui::Combo("Collision", &m, modes, 3)) { mode = (MapEdit::Collision)m; changed = true; }
            if (changed)
            {
                RefreshCollision(m_Sel.group, mode);
                m_Dirty = true;
            }
        }
        else
        {
            ImGui::TextDisabled("Part of the generated border: move / delete only.");
        }

        int blocked = 0;
        for (const auto& b : m_Map.blocks) if (b.tag.group == m_Sel.group) blocked += b.w * b.d;
        ImGui::Text("Blocks %d cell(s) for mobs", blocked);
    }

    if (ImGui::Button("Focus (F)")) FocusSelection(camera);
    ImGui::SameLine();
    if (ImGui::Button("Duplicate (Ctrl+D)")) DuplicateSelection();
    ImGui::SameLine();
    if (ImGui::Button("Delete (Del)")) DeleteSelection();
}
