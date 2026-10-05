// ============================================================
// MapEditMode.cpp
// 戦闘の地図の編集：地図の読み書き・選択・移動・表示（窓は MapEditModeUI.cpp）
// ============================================================
#include "Scene/MapEditMode.h"
#include "Camera/FlyCamera.h"
#include "Component/ModelComponent.h"
#include "Core/Application.h"
#include "Debug/DebugManager.h"
#include "Debug/Gizmo.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Transform.h"
#include "Manager/InputManager.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "World/TerrainGenerator.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <iostream>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kBlockMinSize = 1.2f;  // 足した置物：足跡の長い辺がこれ以上なら衝突を付ける（TerrainGenerator の岩と同じ）

    // 光線 × 軸平行の箱（スラブ法）
    bool RayBox(const Vector3& ro, const Vector3& rd, const Vector3& bmin, const Vector3& bmax, float& tHit)
    {
        const float o[3] = { ro.x, ro.y, ro.z }, d[3] = { rd.x, rd.y, rd.z };
        const float lo[3] = { bmin.x, bmin.y, bmin.z }, hi[3] = { bmax.x, bmax.y, bmax.z };
        float t0 = 0.0f, t1 = FLT_MAX;
        for (int i = 0; i < 3; ++i)
        {
            if (std::fabs(d[i]) < 1e-8f)
            {
                if (o[i] < lo[i] || o[i] > hi[i]) return false;
                continue;
            }
            float ta = (lo[i] - o[i]) / d[i], tb = (hi[i] - o[i]) / d[i];
            if (ta > tb) std::swap(ta, tb);
            t0 = (std::max)(t0, ta);
            t1 = (std::min)(t1, tb);
            if (t0 > t1) return false;
        }
        tHit = t0;
        return true;
    }

    float WrapDeg(float d)
    {
        d = std::fmod(d, 360.0f);
        return (d < 0.0f) ? d + 360.0f : d;
    }
}

void MapEditMode::Shutdown()
{
    for (Entity e : m_Terrain)
        if (m_Reg.IsValid(e)) m_Reg.Destroy(e);
    m_Terrain.clear();
    m_Models.clear();
    m_Loaded = false;
}

void MapEditMode::SetStatus(const std::string& msg)
{
    m_Status = msg;
    std::cout << "[MapEditMode] " << msg << std::endl;
}

// ============================================================
// 地図
// ============================================================
// seed から生成して、その結果（記録）を編集の元にする。生成の実体は捨てる
bool MapEditMode::Generate(uint32_t seed, int biome)
{
    auto* device = Application::Get().GetGraphics().GetDevice();
    Registry tmp;
    GridWorld grid;
    grid.Init(100, 100);   // 戦闘シーンと同じ 200m 四方
    std::vector<Entity> ents;
    TerrainGenerator::Config cfg;
    cfg.seed = seed;
    cfg.biome = (TerrainGenerator::Biome)std::clamp(biome, 0, 2);
    TerrainGenerator::Generate(tmp, device, grid, cfg, ents, nullptr, nullptr, nullptr, &m_Map);
    m_Sel = {};
    m_Loaded = true;
    m_Dirty = true;
    RebuildView();
    SetStatus("Generated from seed " + std::to_string(seed));
    return true;
}

bool MapEditMode::Load(const std::string& name)
{
    MapData::Map loaded;
    if (!MapData::Load(name, loaded))
    {
        SetStatus("Load failed: " + name);
        return false;
    }
    m_Map = std::move(loaded);
    m_Sel = {};
    m_Loaded = true;
    m_Dirty = false;
    strcpy_s(m_NameBuf, name.c_str());
    RebuildView();
    SetStatus("Loaded " + name);
    return true;
}

bool MapEditMode::Save()
{
    if (!m_Loaded || !m_NameBuf[0]) return false;
    const bool ok = MapData::Save(m_NameBuf, m_Map);
    if (ok) m_Dirty = false;
    SetStatus(ok ? std::string("Saved ") + m_NameBuf + ".vmap" : std::string("Save failed: ") + m_NameBuf);
    m_Files = MapData::List();
    return ok;
}

void MapEditMode::RebuildView()
{
    for (Entity e : m_Terrain)
        if (m_Reg.IsValid(e)) m_Reg.Destroy(e);
    m_Terrain.clear();
    if (!m_Loaded) return;
    m_Grid.Init(m_Map.gw, m_Map.gd);
    auto* device = Application::Get().GetGraphics().GetDevice();
    TerrainGenerator::BuildFromMap(m_Reg, device, m_Grid, m_Map, m_Terrain, nullptr, nullptr, nullptr, nullptr,
        TerrainGenerator::kPartVisuals | TerrainGenerator::kPartGround);
}

bool MapEditMode::TestOpen(uint32_t seed, int biome)
{
    m_Active = true;
    DebugManager::Get().SetShowGrid(false);
    return Generate(seed, biome);
}

// ============================================================
// 選択・編集
// ============================================================
// マウスの光線を 0.5m ずつ進めて、地面（高さ場）の下へ入った所を二分で詰める
bool MapEditMode::RayGround(Vector3& hit) const
{
    Vector3 ro, rd;
    Gizmo::GetMouseRay(ro, rd);
    const float halfW = m_Grid.WorldWidth() * 0.5f, halfD = m_Grid.WorldDepth() * 0.5f;
    auto below = [&](float t)
        {
            const Vector3 p = ro + rd * t;
            return std::fabs(p.x) <= halfW && std::fabs(p.z) <= halfD && p.y <= m_Grid.SampleHeight(p.x, p.z);
        };
    float prev = 0.0f;
    for (float t = 0.5f; t < 600.0f; t += 0.5f)
    {
        if (!below(t)) { prev = t; continue; }
        float a = prev, b = t;
        for (int i = 0; i < 12; ++i)
        {
            const float m = (a + b) * 0.5f;
            (below(m) ? b : a) = m;
        }
        hit = ro + rd * b;
        hit.y = m_Grid.SampleHeight(hit.x, hit.z);
        return true;
    }
    return false;
}

MapEditMode::Selection MapEditMode::Pick()
{
    Vector3 ro, rd;
    Gizmo::GetMouseRay(ro, rd);
    Selection best;
    float bestT = FLT_MAX;
    auto test = [&](const std::shared_ptr<Model>& model, const Matrix& world, const Selection& sel)
        {
            if (!model) return;
            const Matrix inv = world.Invert();
            float t = 0.0f;
            if (RayBox(Vector3::Transform(ro, inv), Vector3::TransformNormal(rd, inv),
                    model->GetBoundsMin(), model->GetBoundsMax(), t) && t < bestT)
            {
                bestT = t;
                best = sel;
            }
        };
    for (const auto& p : m_Map.props)
        test(GetModel(m_Map.models[(size_t)p.model]), PropWorld(p), { SelType::Group, p.tag.group, -1 });
    for (int i = 0; i < (int)m_Map.placements.size(); ++i)
    {
        Matrix world;
        auto model = PlacementModel(m_Map.placements[(size_t)i], world);
        test(model, world, { SelType::Placement, 0, i });
    }
    // 手で置いた見えない体積（表示している時だけ選べる）
    if (m_ShowVolumes)
        for (const auto& v : m_Map.volumes)
        {
            float t = 0.0f;
            if (RayBox(ro, rd, v.center - v.half, v.center + v.half, t) && t < bestT)
            {
                bestT = t;
                best = { SelType::Group, v.tag.group, -1 };
            }
        }
    // 地面より奥の物は選ばない（丘の向こうの木を拾わない）
    Vector3 ground;
    const bool hitGround = RayGround(ground);
    if (best.type != SelType::None && hitGround && (ground - ro).Length() + 0.5f < bestT) best = {};
    // 何も拾わなければ、光線が当たった地面の下の地形の部品（台地・高台。坂も同じ 1 個）
    if (best.type == SelType::None && hitGround)
    {
        const uint32_t g = MapTerrainEdit::GroupAt(m_Map, ground.x, ground.z);
        if (g != 0 && IsPartGroup(g)) best = { SelType::Group, g, -1 };
        // 部品も無ければ、その点を含む丘（輪を見せている時だけ）
        else if (m_ShowHills && MapTerrainEdit::CanEditHills(m_Map))
        {
            const uint32_t hg = MapTerrainEdit::HillAt(m_Map, ground.x, ground.z);
            if (hg != 0) best = { SelType::Group, hg, -1 };
        }
    }
    return best;
}

bool MapEditMode::SelectionPos(Vector3& pos) const
{
    if (m_Sel.type == SelType::Group)
    {
        const int pi = MapEdit::FindProp(m_Map, m_Sel.group);
        if (pi >= 0)
        {
            pos = m_Map.props[(size_t)pi].pos;
            return true;
        }
        const int vi = MapEdit::FindVolume(m_Map, m_Sel.group);   // 手で置いた体積
        if (vi >= 0)
        {
            pos = m_Map.volumes[(size_t)vi].center;
            return true;
        }
        // 丘の部品：中心の地面
        if (const int hi = MapTerrainEdit::FindHill(m_Map, m_Sel.group); hi >= 0)
        {
            const MapData::Hill& h = m_Map.hills[(size_t)hi];
            pos = { h.x, m_Grid.SampleHeight(h.x, h.z), h.z };
            return true;
        }
        // 地形の部品：本体の箱の上面の真ん中
        const MapData::BlockPart* body = MapTerrainEdit::Block(const_cast<MapData::Map&>(m_Map), m_Sel.group, 0);
        if (!body)
        {
            // 山頂・洞窟の坂（箱が無い）：坂の真ん中、上端と下端の間の高さ
            const MapData::RampPart* ramp = MapTerrainEdit::Ramp(const_cast<MapData::Map&>(m_Map), m_Sel.group, 0);
            if (!ramp || !IsPartGroup(m_Sel.group)) return false;
            pos = m_Grid.CellToWorld(ramp->x, ramp->z) + Vector3((ramp->w - 1) * GridWorld::kCellSize * 0.5f,
                (ramp->base + ramp->top) * 0.5f, (ramp->d - 1) * GridWorld::kCellSize * 0.5f);
            return true;
        }
        pos = m_Grid.CellToWorld(body->x, body->z)
            + Vector3((body->w - 1) * GridWorld::kCellSize * 0.5f, body->top, (body->d - 1) * GridWorld::kCellSize * 0.5f);
        return true;
    }
    if (m_Sel.type == SelType::Placement && m_Sel.placement >= 0 && m_Sel.placement < (int)m_Map.placements.size())
    {
        pos = m_Map.placements[(size_t)m_Sel.placement].pos;
        return true;
    }
    return false;
}

void MapEditMode::RefreshCollision(uint32_t group, MapEdit::Collision mode)
{
    const int pi = MapEdit::FindProp(m_Map, group);
    if (pi < 0) return;
    auto model = GetModel(m_Map.models[(size_t)m_Map.props[(size_t)pi].model]);
    if (!model) return;
    MapEdit::SetPropCollision(m_Map, group, mode, model->GetBoundsMin(), model->GetBoundsMax());
}

// 選択中を delta だけ動かす。水平にだけ動かした時は地面の高さの差を足す（m_GroundFollow）
bool MapEditMode::ApplyMove(const Vector3& deltaIn)
{
    Vector3 pos;
    if (!SelectionPos(pos)) return false;
    Vector3 delta = deltaIn;
    const bool horizontal = std::fabs(delta.x) + std::fabs(delta.z) > 1e-4f;
    if (m_GroundFollow && horizontal)
        delta.y = m_Grid.SampleHeight(pos.x + delta.x, pos.z + delta.z) - m_Grid.SampleHeight(pos.x, pos.z);
    if (delta.LengthSquared() < 1e-10f) return false;

    if (m_Sel.type == SelType::Placement)
    {
        m_Map.placements[(size_t)m_Sel.placement].pos += delta;
    }
    else if (const int hi = MapTerrainEdit::FindHill(m_Map, m_Sel.group); hi >= 0)
    {
        // 丘の部品：水平に動かして起伏を作り直す。台地などの足元の合わせ直しと見た目は、離した時にまとめて
        m_Map.hills[(size_t)hi].x += deltaIn.x;
        m_Map.hills[(size_t)hi].z += deltaIn.z;
        MapTerrainEdit::RebuildRaw(m_Map);
        MapTerrainEdit::Rederive(m_Map);
        m_ViewDirty = m_ReseatDirty = true;
    }
    else if (IsPartGroup(m_Sel.group))
    {
        // 地形の部品：マス単位で動かす。足元は動かした先の地面に合わせ直し、台座・起伏・高さ場まで作り直す。
        // 見た目（床のメッシュ）の建て直しは重いので、ギズモを離した時にまとめて（m_ViewDirty）
        const int dx = (int)std::lround(deltaIn.x / GridWorld::kCellSize), dz = (int)std::lround(deltaIn.z / GridWorld::kCellSize);
        if (!MapTerrainEdit::MoveGroup(m_Map, m_Sel.group, dx, dz)) return false;
        m_ViewDirty = true;
    }
    else if (const int vi = MapEdit::FindVolume(m_Map, m_Sel.group); vi >= 0)
    {
        // 手で置いた体積：動かして、衝突の箱と塞ぐマスを作り直す
        m_Map.volumes[(size_t)vi].center += delta;
        MapEdit::ApplyVolume(m_Map, m_Sel.group);
    }
    else if (MapEdit::IsSimpleProp(m_Map, m_Sel.group))
    {
        // 置物 1 個の物：位置を動かして、衝突と塞ぐマスを今の付け方のまま作り直す
        const MapEdit::Collision mode = MapEdit::CollisionOf(m_Map, m_Sel.group);
        m_Map.props[(size_t)MapEdit::FindProp(m_Map, m_Sel.group)].pos += delta;
        RefreshCollision(m_Sel.group, mode);
    }
    else
    {
        MapEdit::MoveGroup(m_Map, m_Sel.group, delta);
    }
    m_Dirty = true;
    return true;
}

void MapEditMode::RotateSelection(float deg)
{
    if (m_Sel.type == SelType::Placement && m_Sel.placement >= 0 && m_Sel.placement < (int)m_Map.placements.size())
    {
        float& yaw = m_Map.placements[(size_t)m_Sel.placement].yawDeg;
        yaw = WrapDeg(yaw + deg);
        m_Dirty = true;
    }
    else if (m_Sel.type == SelType::Group && MapEdit::IsSimpleProp(m_Map, m_Sel.group))
    {
        const MapEdit::Collision mode = MapEdit::CollisionOf(m_Map, m_Sel.group);
        float& yaw = m_Map.props[(size_t)MapEdit::FindProp(m_Map, m_Sel.group)].yawDeg;
        yaw = WrapDeg(yaw + deg);
        RefreshCollision(m_Sel.group, mode);
        m_Dirty = true;
    }
}

void MapEditMode::DeleteSelection()
{
    if (m_Sel.type == SelType::Group && MapTerrainEdit::FindHill(m_Map, m_Sel.group) >= 0)
    {
        MapTerrainEdit::DeleteHill(m_Map, m_Sel.group);    // 丘の部品
        MapTerrainEdit::Rederive(m_Map);
        m_ViewDirty = m_ReseatDirty = true;
    }
    else if (m_Sel.type == SelType::Group && IsPartGroup(m_Sel.group))
    {
        MapTerrainEdit::DeleteGroup(m_Map, m_Sel.group);   // 地形の部品：起伏・高さ場も作り直す
        m_ViewDirty = true;
    }
    else if (m_Sel.type == SelType::Group)
        MapEdit::DeleteGroup(m_Map, m_Sel.group);
    else if (m_Sel.type == SelType::Placement && m_Sel.placement >= 0 && m_Sel.placement < (int)m_Map.placements.size())
        m_Map.placements.erase(m_Map.placements.begin() + m_Sel.placement);
    else
        return;
    m_Sel = {};
    m_Dirty = true;
}

void MapEditMode::DuplicateSelection()
{
    if (m_Sel.type == SelType::Placement && m_Sel.placement >= 0 && m_Sel.placement < (int)m_Map.placements.size())
    {
        MapData::Placement p = m_Map.placements[(size_t)m_Sel.placement];
        if (p.type == MapData::kPlaceBossGate) return;   // 門は 1 つだけ
        m_Sel = { SelType::Placement, 0, AddPlacement((MapData::PlaceType)p.type, p.pos + Vector3(2.0f, 0.0f, 0.0f)) };
        return;
    }
    if (m_Sel.type == SelType::Group)
        if (const int hi = MapTerrainEdit::FindHill(m_Map, m_Sel.group); hi >= 0)
        {
            const MapData::Hill h = m_Map.hills[(size_t)hi];   // 丘の部品：隣へ同じ物
            const uint32_t g = MapTerrainEdit::AddHill(m_Map, h.x + h.radius * 2.0f, h.z, h.radius, h.height);
            MapTerrainEdit::Rederive(m_Map);
            m_Sel = { SelType::Group, g, -1 };
            m_Dirty = true;
            m_ViewDirty = m_ReseatDirty = true;
            return;
        }
    if (m_Sel.type == SelType::Group)
        if (const int vi = MapEdit::FindVolume(m_Map, m_Sel.group); vi >= 0)
        {
            const MapData::Volume v = m_Map.volumes[(size_t)vi];
            const uint32_t g = MapEdit::AddVolume(m_Map, v.center + Vector3(v.half.x * 2.0f + 1.0f, 0.0f, 0.0f), v.half,
                v.solid, v.blockMobs);
            m_Sel = { SelType::Group, g, -1 };
            m_Dirty = true;
            return;
        }
    if (m_Sel.type != SelType::Group || !MapEdit::IsSimpleProp(m_Map, m_Sel.group)) return;
    const MapData::Prop src = m_Map.props[(size_t)MapEdit::FindProp(m_Map, m_Sel.group)];
    const MapEdit::Collision mode = MapEdit::CollisionOf(m_Map, m_Sel.group);
    Vector3 pos = src.pos + Vector3(2.0f, 0.0f, 0.0f);
    pos.y += m_Grid.SampleHeight(pos.x, pos.z) - m_Grid.SampleHeight(src.pos.x, src.pos.z);
    const uint32_t g = MapEdit::AddProp(m_Map, m_Map.models[(size_t)src.model], pos, src.yawDeg, src.scale,
        (MapData::Kind)src.tag.kind);
    RefreshCollision(g, mode);
    m_Sel = { SelType::Group, g, -1 };
    m_Dirty = true;
}

void MapEditMode::FocusSelection(FlyCamera& camera)
{
    Vector3 pos;
    if (!SelectionPos(pos)) return;
    float radius = 2.0f;
    if (m_Sel.type == SelType::Group)
    {
        const int pi = MapEdit::FindProp(m_Map, m_Sel.group);
        const int vi = MapEdit::FindVolume(m_Map, m_Sel.group);
        if (pi >= 0)
        {
            const auto& p = m_Map.props[(size_t)pi];
            if (auto model = GetModel(m_Map.models[(size_t)p.model]))
            {
                radius = (model->GetBoundsMax() - model->GetBoundsMin()).Length() * 0.5f * p.scale;
                pos = Vector3::Transform(model->GetBoundsCenter(), PropWorld(p));
            }
        }
        else if (vi >= 0)
        {
            radius = m_Map.volumes[(size_t)vi].half.Length();
        }
        else if (const MapData::BlockPart* body = MapTerrainEdit::Block(m_Map, m_Sel.group, 0))
        {
            radius = Vector3(body->w * GridWorld::kCellSize * 0.5f, body->top - body->base,
                body->d * GridWorld::kCellSize * 0.5f).Length() + 4.0f;
        }
    }
    else if (m_Map.placements[(size_t)m_Sel.placement].type == MapData::kPlaceBossGate)
    {
        radius = kGateHeight * 0.6f;
        pos.y += kGateHeight * 0.5f;
    }
    camera.Focus(pos, (std::max)(radius, 1.0f));
}

int MapEditMode::AddPlacement(MapData::PlaceType type, const Vector3& pos)
{
    if (type == MapData::kPlaceBossGate)   // 門は 1 つだけ：前の物は置き換える
        m_Map.placements.erase(std::remove_if(m_Map.placements.begin(), m_Map.placements.end(),
            [](const MapData::Placement& p) { return p.type == MapData::kPlaceBossGate; }), m_Map.placements.end());
    MapData::Placement p;
    p.type = (uint16_t)type;
    p.pos = pos;
    m_Map.placements.push_back(p);
    m_Dirty = true;
    return (int)m_Map.placements.size() - 1;
}

// 画面の真ん中に見えている地面（足すボタンの置き場所）
Vector3 MapEditMode::ScreenCenterGround(FlyCamera& camera) const
{
    const Vector3 ro = camera.GetPosition(), rd = camera.GetForward();
    for (float t = 1.0f; t < 400.0f; t += 0.5f)
    {
        const Vector3 p = ro + rd * t;
        if (p.y <= m_Grid.SampleHeight(p.x, p.z)) return { p.x, m_Grid.SampleHeight(p.x, p.z), p.z };
    }
    const Vector3 p = ro + rd * 15.0f;
    return { p.x, m_Grid.SampleHeight(p.x, p.z), p.z };
}

// ============================================================
// Update
// ============================================================
bool MapEditMode::Update(FlyCamera& camera, const PlaceRequest& place, float snap)
{
    m_GhostValid = false;
    if (!m_Loaded) return false;

    const ImGuiIO& io = ImGui::GetIO();
    auto& input = InputManager::Get();
    const bool placing = place.path != nullptr;
    bool placed = false;

    // ---- 移動ギズモ（置いている最中は出さない）。クリック処理より先 ----
    Vector3 pivot;
    if (!placing && SelectionPos(pivot))
    {
        Gizmo::Options opt;
        // 地形の部品はマス（2m）単位でしか動かない
        const bool part = m_Sel.type == SelType::Group && IsPartGroup(m_Sel.group);
        opt.snap = part ? GridWorld::kCellSize : snap;
        Vector3 pos = pivot;
        if (Gizmo::Translate("map_edit_selected", pos, opt))
        {
            Vector3 delta = pos - pivot;
            // 水平に動かした時の y は地面に任せる（吸着で y が丸まった分を拾わない）
            if (m_GroundFollow && std::fabs(delta.x) + std::fabs(delta.z) > 1e-4f) delta.y = 0.0f;
            ApplyMove(delta);
        }
    }

    // ---- 置く物をマウスの下の地面へ ----
    if (placing && !io.WantCaptureMouse && !camera.IsLooking())
    {
        Vector3 hit;
        if (RayGround(hit))
        {
            if (snap > 0.0f)
            {
                hit.x = std::round(hit.x / snap) * snap;
                hit.z = std::round(hit.z / snap) * snap;
                hit.y = m_Grid.SampleHeight(hit.x, hit.z);
            }
            m_GhostPos = hit;
            m_Ghost = place;
            m_GhostPath = *place.path;
            m_GhostValid = true;
        }
    }

    // ---- 左クリック：置く / 選ぶ ----
    const bool mouseFree = !io.WantCaptureMouse && !Gizmo::IsHovering() && !Gizmo::IsUsing() && !camera.IsLooking();

    // ---- 地面に塗る道具（区域の筆・起伏の筆。MapEditModeTools.cpp）。有効な間は左クリックで物を選ばない ----
    const bool painting = UpdateTools(mouseFree, placing);

    if (mouseFree && !painting && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (placing)
        {
            auto model = m_GhostValid ? GetModel(m_GhostPath) : nullptr;
            if (model)
            {
                // 底を地面に合わせる。大きい物（足跡の長い辺 1.2m 以上）は衝突を付ける
                const Vector3 lo = model->GetBoundsMin(), hi = model->GetBoundsMax();
                const float u = model->GetFileUnitScale() * place.scale;
                const Vector3 pos(m_GhostPos.x, m_GhostPos.y - lo.y * u, m_GhostPos.z);
                const uint32_t g = MapEdit::AddProp(m_Map, m_GhostPath, pos, place.yawDeg, u);
                const float longSide = (std::max)(hi.x - lo.x, hi.z - lo.z) * u;
                RefreshCollision(g, longSide >= kBlockMinSize ? MapEdit::Collision::Footprint : MapEdit::Collision::None);
                m_Sel = { SelType::Group, g, -1 };
                m_Dirty = true;
                placed = true;
            }
        }
        else
        {
            m_Sel = Pick();
        }
    }

    DrawSelectionMarks();

    // 地形の部品を変えた後の見た目の建て直し：ギズモ・数値のドラッグが終わってから
    if (m_ViewDirty && !Gizmo::IsUsing() && !ImGui::IsAnyItemActive())
    {
        // 起伏（丘の部品・全体の設定）を変えた後：台地などの足元を今の地面へ合わせ直す
        if (m_ReseatDirty)
        {
            m_ReseatDirty = false;
            MapTerrainEdit::ReseatAll(m_Map);
        }
        m_ViewDirty = false;
        RebuildView();
    }

    // ---- ショートカット ----
    if (io.WantTextInput || camera.IsLooking()) return placed;
    const bool ctrl = input.GetKeyPress(VK_CONTROL);
    const bool shift = input.GetKeyPress(VK_SHIFT);
    if (input.GetKeyTrigger('X'))
    {
        if (placing) m_StopPlacing = true;
        else m_Sel = {};
    }
    if (input.GetKeyTrigger(VK_DELETE)) DeleteSelection();
    if (ctrl && input.GetKeyTrigger('D')) DuplicateSelection();
    if (ctrl && input.GetKeyTrigger('S')) Save();
    if (!ctrl && input.GetKeyTrigger('F')) FocusSelection(camera);
    if (!ctrl && !placing && input.GetKeyTrigger('R')) RotateSelection(shift ? -45.0f : 45.0f);
    return placed;
}


bool MapEditMode::IsPartGroup(uint32_t group) const
{
    for (const auto& p : m_Map.blockParts)
        if (p.tag.group == group) return p.tag.kind == MapData::kPlateau || p.tag.kind == MapData::kTerrace;
    // 箱の無い坂 = 山頂の長い坂・洞窟の下り坂（区域を塗り替えられる地図だけ）
    if (MapTerrainEdit::CanEditZones(m_Map))
        for (const auto& p : m_Map.rampParts)
            if (p.tag.group == group) return p.tag.kind == MapData::kSummitRamp || p.tag.kind == MapData::kMineRamp;
    return false;
}
