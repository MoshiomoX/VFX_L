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
#include "Player/WalletComponent.h"
#include "Graphics/Model/Model.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "imgui.h"

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
// 山頂・洞窟のマスの表があれば、そこからも同じ条件で m_SummitCount / m_MineCount 個
// （区域の中の箱同士は区域の広さに合わせて 20m 以上離す）。
// 乱数は地形の seed から作る（同じ seed なら同じ配置）。
// 箱は静的な AABB を持つ（プレイヤーは押し返される。雑魚は GPU 側なので素通り）
// ============================================================
void RewardCrateSystem::Spawn(Registry& reg, const GridWorld& grid, const Vector3& center,
    uint32_t seed, InteractionSystem& interaction,
    const std::vector<int>* summitCells, const std::vector<int>* mineCells,
    const std::vector<DirectX::SimpleMath::Vector4>* fixed)
{
    for (Entity e : m_Crates)
        if (reg.IsValid(e)) reg.Destroy(e);
    m_Crates.clear();
    interaction.ClearFocus();
    m_Opened = 0;   // 並べ直したら値段も最初から（面の始まり）

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

    // 箱 1 個の実体（pos = 底の中心、a = 向き rad）
    auto spawnAt = [&](const Vector3& pos, float a)
    {
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
    };

    // マス (gx, gz) の中心に 1 個置けたら true。spacing = 既に置いた箱からの最小距離
    auto tryPlace = [&](int gx, int gz, float a, float spacing) -> bool
    {
        bool open = true;
        for (int dz = -1; dz <= 1 && open; ++dz)
            for (int dx = -1; dx <= 1 && open; ++dx)
                open = grid.IsWalkable(gx + dx, gz + dz);
        if (!open) return false;

        Vector3 pos = grid.CellToWorld(gx, gz);
        pos.y = grid.SampleHeight(pos.x, pos.z);

        // 周り 3x3 もほぼ同じ高さ（坂道・崖の縁には置かない）。2026-10-04 から地面に起伏（緩い丘で隣のマスと
        // 2m で 0.2〜0.5m 違う）があるので 0.2 → 0.6m。台地の縁の段差（2.5m 以上）は引き続き弾く
        bool flat = true;
        for (int dz = -1; dz <= 1 && flat; ++dz)
            for (int dx = -1; dx <= 1 && flat; ++dx)
            {
                const Vector3 c = grid.CellToWorld(gx + dx, gz + dz);
                flat = std::fabs(grid.SampleHeight(c.x, c.z) - pos.y) < 0.6f;
            }
        if (!flat) return false;

        for (Entity other : m_Crates)
        {
            const Vector3 op = reg.Get<InteractableComponent>(other).basePos;
            const float dx = op.x - pos.x, dz = op.z - pos.z;
            if (dx * dx + dz * dz < spacing * spacing) return false;
        }

        spawnAt(pos, a);
        return true;
    };

    // 地図に箱が置いてあればその通りに（地図エディタ。場所の検査はしない = 置いた人の責任）
    if (fixed && !fixed->empty())
    {
        for (const auto& f : *fixed)
            spawnAt({ f.x, f.y, f.z }, DirectX::XMConvertToRadians(f.w));
        std::cout << "[RewardCrateSystem] reward crates: " << m_Crates.size() << " from the map" << std::endl;
        return;
    }

    for (int attempt = 0, placed = 0; attempt < 400 && placed < m_Count; ++attempt)
    {
        const float a = angleDist(rng);
        const float r = radiusDist(rng);
        const Vector3 probe = center + Vector3(std::cos(a) * r, 0.0f, std::sin(a) * r);
        int gx = 0, gz = 0;
        grid.WorldToCell(probe, gx, gz);
        if (tryPlace(gx, gz, a, m_Spacing)) ++placed;
    }

    // 山頂・洞窟（区域のマスから無作為に）
    auto placeInZone = [&](const std::vector<int>* cells, int count) -> int
    {
        if (!cells || cells->empty()) return 0;
        std::uniform_int_distribution<int> pick(0, (int)cells->size() - 1);
        int placed = 0;
        for (int attempt = 0; attempt < 400 && placed < count; ++attempt)
        {
            const int c = (*cells)[pick(rng)];
            if (tryPlace(c % grid.Width(), c / grid.Width(), angleDist(rng), (std::max)(m_Spacing, 20.0f))) ++placed;
        }
        return placed;
    };
    const int nearCount = (int)m_Crates.size();
    const int summitPlaced = placeInZone(summitCells, m_SummitCount);
    const int minePlaced = placeInZone(mineCells, m_MineCount);

    std::cout << "[RewardCrateSystem] reward crates: near " << nearCount << " / " << m_Count
        << ", summit " << summitPlaced << " / " << m_SummitCount << ", mine " << minePlaced << " / " << m_MineCount
        << " (scale " << scale << ")" << std::endl;
}

// ============================================================
// 使われた物が報酬の箱なら、レベルアップと同じ三択を出す（レベルは上がらない）
// ============================================================
bool RewardCrateSystem::TryOpen(Registry& reg, Entity used, Entity player, LevelUpSystem& levelUp,
    InteractionSystem& interaction, Vector3& openedPos)
{
    if (used == EntityTraits::NULL_ENTITY || !reg.IsValid(used) || !reg.Has<InteractableComponent>(used))
        return false;

    const auto& it = reg.Get<InteractableComponent>(used);
    if (it.kind != InteractKind::RewardChoice) return false;

    // 金貨（2026-10-04）。足りなければ箱は残す。払うのは三択が出せた時だけ
    const int price = Price();
    WalletComponent* wallet = reg.Has<WalletComponent>(player) ? &reg.Get<WalletComponent>(player) : nullptr;
    if (wallet && !wallet->CanPay(price))
    {
        m_Denied = true;
        return false;
    }
    if (!levelUp.OfferChoices(reg, player)) return false;
    if (wallet) wallet->Pay(price);
    ++m_Opened;

    openedPos = it.basePos;
    reg.Destroy(used);
    m_Crates.erase(std::remove(m_Crates.begin(), m_Crates.end(), used), m_Crates.end());
    interaction.ClearFocus();
    return true;
}

int RewardCrateSystem::Price() const
{
    return (int)std::lround((float)basePrice * std::pow(priceGrowth, (float)m_Opened));
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
    ImGui::Text("Opened %d   next price %d gold", m_Opened, Price());
    ImGui::DragInt("Base Price", &basePrice, 1, 0, 1000);
    ImGui::DragFloat("Price Growth x", &priceGrowth, 0.01f, 1.0f, 3.0f);
    ImGui::DragInt("Count (near start)", &m_Count, 1, 0, 30);
    ImGui::DragInt("Count (summit)", &m_SummitCount, 1, 0, 10);
    ImGui::DragInt("Count (mine)", &m_MineCount, 1, 0, 10);
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
