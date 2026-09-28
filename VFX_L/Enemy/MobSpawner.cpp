// ============================================================
// MobSpawner.cpp
// ============================================================
#include "Enemy/MobSpawner.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/AreaProfile.h"
#include "World/GridWorld.h"
#include "imgui.h"
#include <cmath>
#include <cstdlib>

using DirectX::SimpleMath::Vector3;

namespace
{
    // 面板のボタンで湧かせる自爆兵の輪（玩家から m）
    constexpr float kDebugRingMin = 8.0f;
    constexpr float kDebugRingMax = 12.0f;

    float Rand01() { return (float)rand() / RAND_MAX; }
}

void MobSpawner::Init(SwarmSystem& swarm)
{
    swarm.SetRecycleMinDist(m_Director.rMax);

    // 爆発の見た目の範囲。AreaProfileDB::LoadAll の後に呼ばれる（無ければ 0 = 見た目無しで爆発だけ）
    swarm.GetBomberParams().blastArea = (uint32_t)AreaProfileDB::IndexOf("BomberBlast");
}

void MobSpawner::Request(SwarmSystem& swarm, const Vector3& pos, bool recycle)
{
    const float groundY = swarm.GetAIParams().groundY;
    const Vector3 p = { pos.x, groundY, pos.z };
    const bool bomber = Rand01() < m_BomberRatio;
    const float hp = bomber ? m_BomberHp : m_MobHp;
    const float speed = bomber ? m_BomberSpeed : m_MobSpeed;
    const uint32_t kind = bomber ? Swarm::kEnemyKindBomber : Swarm::kEnemyKindMob;

    if (recycle) swarm.RecycleEnemy(p, hp, speed, kind);
    else         swarm.SpawnEnemy(p, hp, speed, kind);
}

void MobSpawner::SpawnDebugBombers(const GridWorld& grid, const Vector3& player, SwarmSystem& swarm)
{
    const float groundY = swarm.GetAIParams().groundY;
    for (; m_DebugBombers > 0; --m_DebugBombers)
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const float ang = Rand01() * 6.2831853f;
            const float r = kDebugRingMin + (kDebugRingMax - kDebugRingMin) * Rand01();
            const Vector3 pos = player + Vector3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);

            int gx = 0, gz = 0;
            grid.WorldToCell(pos, gx, gz);
            if (!grid.IsWalkable(gx, gz)) continue;

            swarm.SpawnEnemy({ pos.x, groundY, pos.z }, m_BomberHp, m_BomberSpeed, Swarm::kEnemyKindBomber);
            break;
        }
    }
}

void MobSpawner::Update(const GridWorld& grid, const Vector3& player, float dt, SwarmSystem& swarm)
{
    swarm.SetRecycleMinDist(m_Director.rMax);

    m_Director.Update(grid, player, dt,
        (int)swarm.GetCounters().aliveEnemies,
        [this, &swarm](const Vector3& pos) { Request(swarm, pos, false); },
        [this, &swarm](const Vector3& pos) { Request(swarm, pos, true); });

    SpawnDebugBombers(grid, player, swarm);
}

// ============================================================
// ImGui: Enemies 面板の雑魚の段
// ============================================================
void MobSpawner::DrawImGui(SwarmSystem& swarm)
{
    // ---- 湧き管理 ----
    ImGui::Checkbox("Director Enabled", &m_Director.enabled);
    ImGui::Text("Mobs : %d / %d   (spawned %d, recycled %d)",
        m_Director.GetLastMobCount(), m_Director.spawnCap,
        m_Director.GetTotalSpawned(), m_Director.GetTotalRecycled());
    ImGui::DragInt("Spawn Cap", &m_Director.spawnCap, 1, 0, 4096);
    ImGui::DragFloat("Interval", &m_Director.spawnInterval, 0.05f, 0.1f, 10.0f);
    ImGui::DragInt("Per Tick", &m_Director.spawnPerTick, 1, 1, 20);
    ImGui::DragFloat("Ring Min", &m_Director.rMin, 0.5f, 5.0f, 100.0f);
    ImGui::DragFloat("Ring Max", &m_Director.rMax, 0.5f, 5.0f, 120.0f);
    ImGui::TextDisabled("recycle: enemies farther than Ring Max get teleported");
    ImGui::Separator();

    // ---- 雑魚の初期値 ----
    ImGui::DragFloat("Mob HP", &m_MobHp, 1.0f, 1.0f, 1000.0f);
    ImGui::DragFloat("Mob Speed", &m_MobSpeed, 0.1f, 0.0f, 20.0f);
    ImGui::Separator();

    // ---- 自爆兵（湧きの割合・初期値は CPU、導火線と爆発は GPU の定数）----
    auto& bomb = swarm.GetBomberParams();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1), "Bomber (GPU)");
    ImGui::SliderFloat("Bomber Ratio", &m_BomberRatio, 0.0f, 1.0f, "%.2f");
    ImGui::DragFloat("Bomber HP", &m_BomberHp, 1.0f, 1.0f, 1000.0f);
    ImGui::DragFloat("Bomber Speed", &m_BomberSpeed, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Fuse Time (s)", &bomb.fuseTime, 0.05f, 0.1f, 5.0f);
    ImGui::DragFloat("Trigger Margin", &bomb.triggerMargin, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Blast Radius", &bomb.blastRadius, 0.05f, 0.5f, 10.0f);
    ImGui::DragFloat("Blast Damage", &bomb.blastDamage, 0.5f, 0.0f, 200.0f);
    ImGui::DragFloat("Fuse Swell", &bomb.swell, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Fuse Flash Gain", &bomb.flashGain, 0.1f, 1.0f, 10.0f);
    ImGui::Text("Blast VFX area: #%u (AreaData/BomberBlast.json)", bomb.blastArea);
    auto& ring = swarm.bomberRing;   // 点火中の足元の警告の輪（外周 = Blast Radius）
    ImGui::Checkbox("Warning Ring", &ring.enabled);
    ImGui::DragFloat("Ring Edge Width", &ring.edgeWidth, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat("Ring Lift", &ring.lift, 0.005f, 0.0f, 0.3f);
    ImGui::ColorEdit4("Ring Fill", &ring.fill.x);
    ImGui::ColorEdit4("Ring Edge", &ring.edge.x);
    ImGui::ColorEdit4("Ring Back", &ring.back.x);
    if (ImGui::Button("Spawn 5 Bombers Nearby")) QueueDebugBombers(5);
    ImGui::Separator();

    // ---- 雑魚 AI（GPU の定数。次の固定ステップから効く）----
    auto& ai = swarm.GetAIParams();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Mob AI (GPU)");
    ImGui::DragFloat("Player Push Out", &ai.playerPushOut, 0.5f, 0.0f, 40.0f);
    ImGui::DragFloat("Separation Radius", &ai.separationRadius, 0.05f, 0.5f, 5.0f);
    ImGui::DragFloat("Separation Power", &ai.separationPower, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Avoid Power", &ai.avoidPower, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Look Ahead", &ai.lookAhead, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Ground Y", &ai.groundY, 0.01f, -1.0f, 3.0f);
    ImGui::DragFloat("Enemy Radius", &ai.enemyRadius, 0.01f, 0.1f, 2.0f);
    ImGui::DragFloat("Velocity Lag", &ai.velocityLag, 0.5f, 1.0f, 40.0f);
    ImGui::DragFloat("Max Speed Mul", &ai.maxSpeedMul, 0.05f, 1.0f, 4.0f);
    ImGui::DragFloat("Turn Speed", &ai.turnSpeed, 0.5f, 1.0f, 40.0f);

    ImGui::DragFloat("Contact Damage", &ai.contactDamage, 0.5f, 0.0f, 100.0f);
    ImGui::DragFloat("Attack Interval", &ai.attackInterval, 0.05f, 0.1f, 5.0f);
    ImGui::DragFloat("Hit Stun (s)", &ai.hitStun, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat("Hit Flash Gain", &ai.hitFlash, 0.1f, 1.0f, 10.0f);
}
