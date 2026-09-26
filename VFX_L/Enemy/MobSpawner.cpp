// ============================================================
// MobSpawner.cpp
// ============================================================
#include "Enemy/MobSpawner.h"
#include "Swarm/SwarmSystem.h"
#include "World/GridWorld.h"
#include "imgui.h"

using DirectX::SimpleMath::Vector3;

void MobSpawner::Init(SwarmSystem& swarm)
{
    swarm.SetRecycleMinDist(m_Director.rMax);
}

void MobSpawner::Update(const GridWorld& grid, const Vector3& player, float dt, SwarmSystem& swarm)
{
    swarm.SetRecycleMinDist(m_Director.rMax);

    const float groundY = swarm.GetAIParams().groundY;
    m_Director.Update(grid, player, dt,
        (int)swarm.GetCounters().aliveEnemies,
        [this, &swarm, groundY](const Vector3& pos)
        {
            swarm.SpawnEnemy({ pos.x, groundY, pos.z }, m_MobHp, m_MobSpeed);
        },
        [this, &swarm, groundY](const Vector3& pos)
        {
            swarm.RecycleEnemy({ pos.x, groundY, pos.z }, m_MobHp, m_MobSpeed);
        });
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
