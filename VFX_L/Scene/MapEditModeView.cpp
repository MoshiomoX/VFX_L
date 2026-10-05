// ============================================================
// MapEditModeView.cpp
// 戦闘の地図の編集：表示（モデルの取得・置物と機能付きの置き物の行列・選んだ物の目印・描画）
// ============================================================
#include "Scene/MapEditMode.h"
#include "Component/ModelComponent.h"
#include "Debug/DebugManager.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Transform.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"

using namespace DirectX::SimpleMath;

namespace
{
    // 箱（lo〜hi を world で写した物）を線で
    void LineBox(const Vector3& lo, const Vector3& hi, const Matrix& world, const Color& color)
    {
        Vector3 c[8];
        for (int i = 0; i < 8; ++i)
            c[i] = Vector3::Transform(Vector3((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z), world);
        static const int kEdges[12][2] = {
            { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        auto& dbg = DebugManager::Get();
        for (const auto& e : kEdges) dbg.AddDebugLine(c[e[0]], c[e[1]], color);
    }

}

// ============================================================
// 表示の補助
// ============================================================
std::shared_ptr<Model> MapEditMode::GetModel(const std::string& path)
{
    auto it = m_Models.find(path);
    if (it != m_Models.end()) return it->second;   // 読めなかった物も nullptr で覚える
    auto model = ResourceManager::Get().LoadModel(path);
    m_Models[path] = model;
    return model;
}

Matrix MapEditMode::PropWorld(const MapData::Prop& p) const
{
    return Matrix::CreateScale(p.scale) * Matrix::CreateRotationY(DirectX::XMConvertToRadians(p.yawDeg))
        * Matrix::CreateTranslation(p.pos);
}

// 機能付きの置き物の見た目（戦闘シーンと同じ大きさ・置き方）。pos = 底の中心
std::shared_ptr<Model> MapEditMode::PlacementModel(const MapData::Placement& p, Matrix& outWorld)
{
    const bool gate = p.type == MapData::kPlaceBossGate;
    auto model = GetModel(gate ? Res::Mdl::Ruins_ArchGate : Res::Mdl::Kenney_RewardCrate);
    if (!model) return nullptr;
    const Vector3 lo = model->GetBoundsMin(), hi = model->GetBoundsMax();
    const float h = hi.y - lo.y;
    const float scale = (h > 1e-4f) ? (gate ? kGateHeight : kCrateSize) / h : 1.0f;
    const Matrix rot = Matrix::CreateRotationY(DirectX::XMConvertToRadians(p.yawDeg));
    Vector3 origin = p.pos;
    if (gate)   // 包囲箱の xz の真ん中を pos に、底を地面に（StageDirector::SpawnPortal と同じ）
        origin = p.pos - Vector3::Transform(Vector3((lo.x + hi.x) * 0.5f * scale, 0.0f, (lo.z + hi.z) * 0.5f * scale), rot)
            - Vector3(0.0f, lo.y * scale, 0.0f);
    outWorld = Matrix::CreateScale(scale) * rot * Matrix::CreateTranslation(origin);
    return model;
}

// 選んだ物：見た目の箱（黄）、衝突（緑）、塞いだマス（赤い枠、地面の少し上）
void MapEditMode::DrawSelectionMarks()
{
    // 手で置いた見えない体積：全部を水色の線で（選んだ物は黄。プレイヤーに当たらない物は暗い色）
    if (m_ShowVolumes)
        for (const auto& v : m_Map.volumes)
        {
            const bool selected = m_Sel.type == SelType::Group && m_Sel.group == v.tag.group;
            const Color c = selected ? Color(1.0f, 0.85f, 0.2f, 1.0f)
                : v.solid ? Color(0.3f, 0.85f, 1.0f, 1.0f) : Color(0.25f, 0.45f, 0.7f, 1.0f);
            LineBox(v.center - v.half, v.center + v.half, Matrix::Identity, c);
        }

    if (m_Sel.type == SelType::Placement && m_Sel.placement >= 0 && m_Sel.placement < (int)m_Map.placements.size())
    {
        Matrix world;
        if (auto model = PlacementModel(m_Map.placements[(size_t)m_Sel.placement], world))
            LineBox(model->GetBoundsMin(), model->GetBoundsMax(), world, Color(1.0f, 0.85f, 0.2f, 1.0f));
        return;
    }
    if (m_Sel.type != SelType::Group) return;

    for (const auto& p : m_Map.props)
        if (p.tag.group == m_Sel.group)
            if (auto model = GetModel(m_Map.models[(size_t)p.model]))
                LineBox(model->GetBoundsMin(), model->GetBoundsMax(), PropWorld(p), Color(1.0f, 0.85f, 0.2f, 1.0f));
    if (!m_ShowCollision) return;

    const Color green(0.2f, 1.0f, 0.3f, 1.0f), red(1.0f, 0.25f, 0.2f, 1.0f);
    for (const auto& b : m_Map.boxes)
        if (b.tag.group == m_Sel.group) LineBox(b.lo, b.hi, Matrix::Identity, green);
    auto& dbg = DebugManager::Get();
    for (const auto& h : m_Map.hulls)
    {
        if (h.tag.group != m_Sel.group) continue;
        for (int i = 0; i < 4; ++i)   // 下の輪・上の輪・縦
        {
            dbg.AddDebugLine(h.v[i], h.v[(i + 1) % 4], green);
            dbg.AddDebugLine(h.v[i + 4], h.v[(i + 1) % 4 + 4], green);
            dbg.AddDebugLine(h.v[i], h.v[i + 4], green);
        }
    }
    const float cs = GridWorld::kCellSize;
    for (const auto& b : m_Map.blocks)
    {
        if (b.tag.group != m_Sel.group) continue;
        for (int z = b.z; z < b.z + b.d; ++z)
            for (int x = b.x; x < b.x + b.w; ++x)
            {
                const Vector3 c = m_Grid.CellToWorld(x, z);
                const float y = m_Grid.SampleHeight(c.x, c.z) + 0.15f;
                const float h = cs * 0.5f - 0.05f;
                const Vector3 q[4] = { { c.x - h, y, c.z - h }, { c.x + h, y, c.z - h }, { c.x + h, y, c.z + h }, { c.x - h, y, c.z + h } };
                for (int i = 0; i < 4; ++i) dbg.AddDebugLine(q[i], q[(i + 1) % 4], red);
            }
    }
}

// ============================================================
// Render
// 地形（床・合成モデル）→ 置物 → 機能付きの置き物 → 置こうとしている物
// ============================================================
void MapEditMode::Render(Renderer& renderer)
{
    if (!m_Loaded) return;

    Transform identity;
    for (Entity e : m_Terrain)
    {
        if (!m_Reg.IsValid(e) || !m_Reg.Has<ModelComponent>(e)) continue;
        const auto& mc = m_Reg.Get<ModelComponent>(e);
        if (mc.model) mc.model->Draw(renderer, &identity);
    }

    for (const auto& p : m_Map.props)
    {
        auto model = GetModel(m_Map.models[(size_t)p.model]);
        if (!model) continue;
        Transform t;
        t.SetPosition(p.pos);
        t.SetRotation({ 0.0f, p.yawDeg, 0.0f });
        t.SetScale({ p.scale, p.scale, p.scale });
        model->Draw(renderer, &t);
    }

    for (const auto& pl : m_Map.placements)
    {
        Matrix world;
        auto model = PlacementModel(pl, world);
        if (!model) continue;
        Vector3 scale, pos;
        Quaternion rot;
        world.Decompose(scale, rot, pos);
        Transform t;
        t.SetPosition(pos);
        t.SetRotation({ 0.0f, pl.yawDeg, 0.0f });
        t.SetScale(scale);
        model->Draw(renderer, &t);
    }

    if (m_GhostValid)
    {
        if (auto model = GetModel(m_GhostPath))
        {
            const float u = model->GetFileUnitScale() * m_Ghost.scale;
            Transform t;
            t.SetPosition({ m_GhostPos.x, m_GhostPos.y - model->GetBoundsMin().y * u, m_GhostPos.z });
            t.SetRotation({ 0.0f, m_Ghost.yawDeg, 0.0f });
            t.SetScale({ u, u, u });
            model->Draw(renderer, &t);
        }
    }
}
