// ============================================================
// RewardCrateSystem.cpp
// ============================================================
#include "ECS/System/RewardCrateSystem.h"
#include "ECS/System/InteractionSystem.h"
#include "UI/LevelUpSystem.h"
#include "World/GridWorld.h"
#include "Component/TransformComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/InteractableComponent.h"
#include "Graphics/Model/Model.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

using DirectX::SimpleMath::Vector3;

void RewardCrateSystem::Init()
{
    m_Crates.clear();
    m_Model = ResourceManager::Get().LoadModel(Res::Mdl::Kenney_RewardCrate);
}

// ============================================================
// 報酬の箱を並べ直す
// center の周り（m_MinDist〜m_MaxDist）の歩けるマスへ m_Count 個。
//   - 周り 3x3 マスも歩ける所だけ（壁際に置くと回り込めない）
//   - 箱同士は m_Spacing 以上離す
//   - マスの中心・地面の高さに置く
// 乱数は地形の seed から作る（同じ seed なら同じ配置）。
// 箱は静的な AABB を持つ（玩家は押し返される。雑魚は GPU 側なので素通り）
// ============================================================
void RewardCrateSystem::Spawn(Registry& reg, const GridWorld& grid, const Vector3& center,
    uint32_t seed, InteractionSystem& interaction)
{
    for (Entity e : m_Crates)
        if (reg.IsValid(e)) reg.Destroy(e);
    m_Crates.clear();
    interaction.ClearFocus();

    if (!m_Model)
    {
        std::cout << "[RewardCrateSystem] reward crate model missing: " << Res::Mdl::Kenney_RewardCrate << std::endl;
        return;
    }

    // 倍率はモデルの包囲箱から（ファイルの単位 cm / m に頼らない）。原点は底の中心
    const Vector3 lo = m_Model->GetBoundsMin();
    const Vector3 hi = m_Model->GetBoundsMax();
    const float height = hi.y - lo.y;
    const float scale = (height > 1e-4f) ? m_Size / height : 1.0f;
    const float half = m_Size * 0.5f;

    std::mt19937 rng(seed * 7919u + 17u);
    std::uniform_real_distribution<float> angleDist(0.0f, DirectX::XM_2PI);
    std::uniform_real_distribution<float> radiusDist(m_MinDist, (std::max)(m_MinDist, m_MaxDist));

    for (int attempt = 0; attempt < 400 && (int)m_Crates.size() < m_Count; ++attempt)
    {
        const float a = angleDist(rng);
        const float r = radiusDist(rng);
        const Vector3 probe = center + Vector3(std::cos(a) * r, 0.0f, std::sin(a) * r);

        int gx = 0, gz = 0;
        grid.WorldToCell(probe, gx, gz);
        bool open = true;
        for (int dz = -1; dz <= 1 && open; ++dz)
            for (int dx = -1; dx <= 1 && open; ++dx)
                open = grid.IsWalkable(gx + dx, gz + dz);
        if (!open) continue;

        Vector3 pos = grid.CellToWorld(gx, gz);
        pos.y = grid.SampleHeight(pos.x, pos.z);

        bool tooClose = false;
        for (Entity other : m_Crates)
        {
            const Vector3 op = reg.Get<InteractableComponent>(other).basePos;
            const float dx = op.x - pos.x, dz = op.z - pos.z;
            if (dx * dx + dz * dz < m_Spacing * m_Spacing) { tooClose = true; break; }
        }
        if (tooClose) continue;

        Entity e = reg.Create();

        TransformComponent tf;
        tf.position = pos;
        tf.rotation = { 0.0f, DirectX::XMConvertToDegrees(a), 0.0f };   // 向きもばらす
        tf.scale = { scale, scale, scale };
        reg.Add<TransformComponent>(e, tf);

        ModelComponent mc;
        mc.model = m_Model;
        reg.Add<ModelComponent>(e, mc);

        ColliderComponent col;
        col.shape = ColliderShape::AABB;
        col.halfExtents = { half, half, half };
        col.offset = { 0.0f, half, 0.0f };   // モデルの原点が底なので、箱の中心は半分上
        col.layer = Layer_Terrain;
        col.mask = Layer_All;
        reg.Add<ColliderComponent>(e, col);

        RigidbodyComponent rb;
        rb.isStatic = true;
        rb.useGravity = false;
        reg.Add<RigidbodyComponent>(e, rb);

        InteractableComponent it;
        it.kind = InteractKind::RewardChoice;
        it.basePos = pos;
        it.phase = a * 3.0f;
        it.radius = half + 1.6f;   // 箱の縁から 1.6m くらいまで
        reg.Add<InteractableComponent>(e, it);

        m_Crates.push_back(e);
    }

    std::cout << "[RewardCrateSystem] reward crates: " << m_Crates.size() << " / " << m_Count
        << " (scale " << scale << ")" << std::endl;
}

// ============================================================
// 使われた物が報酬の箱なら、升級と同じ三択を出す（レベルは上がらない）
// ============================================================
bool RewardCrateSystem::TryOpen(Registry& reg, Entity used, Entity player, LevelUpSystem& levelUp,
    InteractionSystem& interaction, Vector3& openedPos)
{
    if (used == EntityTraits::NULL_ENTITY || !reg.IsValid(used) || !reg.Has<InteractableComponent>(used))
        return false;

    const auto& it = reg.Get<InteractableComponent>(used);
    if (it.kind != InteractKind::RewardChoice) return false;
    if (!levelUp.OfferChoices(reg, player)) return false;

    openedPos = it.basePos;
    reg.Destroy(used);
    m_Crates.erase(std::remove(m_Crates.begin(), m_Crates.end(), used), m_Crates.end());
    interaction.ClearFocus();
    return true;
}

// ============================================================
// ImGui: 報酬の箱
// ============================================================
bool RewardCrateSystem::DrawImGui(InteractionSystem& interaction, const LevelUpSystem& levelUp)
{
    if (!ImGui::CollapsingHeader("Reward Crates"))
        return false;

    ImGui::Text("Remaining : %d   offers so far : %d", (int)m_Crates.size(), levelUp.GetTotalOffers());
    ImGui::Text("Focus     : %s", interaction.HasFocus() ? "YES (press F / Pad B)" : "none");
    ImGui::DragInt("Count", &m_Count, 1, 0, 30);
    ImGui::DragFloatRange2("Distance (m)", &m_MinDist, &m_MaxDist, 0.5f, 0.0f, 90.0f);
    ImGui::DragFloat("Spacing (m)", &m_Spacing, 0.1f, 0.0f, 30.0f);
    ImGui::DragFloat("Size (m)", &m_Size, 0.01f, 0.1f, 3.0f);
    ImGui::DragFloat("Bob Height", &interaction.bobHeight, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Spin (deg/s)", &interaction.spinSpeed, 1.0f, 0.0f, 360.0f);
    const bool respawn = ImGui::Button("Respawn Crates");
    ImGui::SameLine();
    ImGui::TextDisabled("around the player");
    return respawn;
}
