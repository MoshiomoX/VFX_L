// ============================================================
// StageDirector.cpp
// ============================================================
#include "Enemy/StageDirector.h"
#include "Enemy/MobSpawner.h"
#include "Swarm/SwarmSystem.h"
#include "World/GridWorld.h"
#include "ECS/Registry.h"
#include "ECS/System/InteractionSystem.h"
#include "Component/TransformComponent.h"
#include "Component/ModelComponent.h"
#include "Component/InteractableComponent.h"
#include "Graphics/Model/Model.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "Debug/TestSpawner.h"
#include <iostream>
#include <random>
#include <utility>
#include <vector>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;

namespace
{
    float Rand01() { return (float)rand() / RAND_MAX; }
}

void StageDirector::Init()
{
    m_PortalModel = ResourceManager::Get().LoadModel(Res::Mdl::Ruins_ArchGate);   // 石の拱（2026-10-03）
    m_Portal = EntityTraits::NULL_ENTITY;
    m_Boss = BossState::None;
}

bool StageDirector::SpawnElite(const GridWorld& grid, const Vector3& player, float hp, SwarmSystem& swarm)
{
    const float groundY = swarm.GetAIParams().groundY;
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const float ang = Rand01() * 6.2831853f;
        const float r = eliteRingMin + (eliteRingMax - eliteRingMin) * Rand01();
        const Vector3 pos = player + Vector3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);

        int gx = 0, gz = 0;
        grid.WorldToCell(pos, gx, gz);
        if (!grid.IsWalkable(gx, gz)) continue;

        swarm.SpawnEnemy({ pos.x, grid.SampleHeight(pos.x, pos.z) + groundY, pos.z }, hp, eliteSpeed, Swarm::kEnemyKindElite);
        ++m_ElitesSpawned;
        std::cout << "[Stage] elite #" << m_ElitesSpawned << " hp " << hp << std::endl;
        return true;
    }
    return false;   // 歩ける所が見つからない。次のフレームでもう一度
}

bool StageDirector::SpawnBoss(const GridWorld& grid, const Vector3& player, SwarmSystem& swarm)
{
    const float groundY = swarm.GetAIParams().groundY;
    for (int attempt = 0; attempt < 24; ++attempt)
    {
        const float ang = Rand01() * 6.2831853f;
        const float r = bossRingMin + (bossRingMax - bossRingMin) * Rand01();
        const Vector3 pos = player + Vector3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);

        int gx = 0, gz = 0;
        grid.WorldToCell(pos, gx, gz);
        if (!grid.IsWalkable(gx, gz)) continue;

        swarm.SpawnEnemy({ pos.x, grid.SampleHeight(pos.x, pos.z) + groundY, pos.z }, m_BossSpawnHp, bossSpeed,
            Swarm::kEnemyKindBoss);
        std::cout << "[Stage] boss summoned, hp " << m_BossSpawnHp << std::endl;
        return true;
    }
    return false;
}

void StageDirector::Update(const GridWorld& grid, const Vector3& player, float runTime, float dt,
    MobSpawner& mobs, SwarmSystem& swarm)
{
    // ---- 時間切れの後は最終波 ----
    auto& director = mobs.Director();
    if (runTime >= stageTime)
    {
        const float over = runTime - stageTime;
        const int level = 1 + (int)(over / (std::max)(finalStepTime, 1.0f));
        if (level != m_FinalLevel)
        {
            if (m_FinalLevel == 0)
            {
                m_NormalCap = director.spawnCap;
                m_Event = "final swarm";
            }
            else
                m_Event = "final swarm step";
            m_FinalLevel = level;
            std::cout << "[Stage] final swarm level " << level << std::endl;
        }
        mobs.finalStatMul = std::pow(finalStepMul, (float)(level - 1));
        mobs.finalSpawnRate = finalSpawnRate;
        mobs.finalSpeedMul = finalSpeedMul;
        mobs.finalGhostRate = finalGhostRate * std::pow(finalGhostStepMul, (float)(level - 1));   // 幽霊は段毎に数が増える
        director.spawnCap = (over < 120.0f) ? finalCap : finalCapLate;
    }
    else if (m_FinalLevel != 0)
    {
        // 面板で制限時間を延ばした時など: 普段へ戻す
        m_FinalLevel = 0;
        mobs.finalStatMul = 1.0f;
        mobs.finalSpawnRate = 0.0f;
        mobs.finalSpeedMul = 1.0f;
        mobs.finalGhostRate = 0.0f;
        if (m_NormalCap >= 0) director.spawnCap = m_NormalCap;
    }

    const float hp = eliteHp * mobs.GetStatMul();   // 出た瞬間の難度で決まる（雑魚と同じ）

    // ---- 決まった時間に精英 ----
    if (m_NextElite < eliteTimes.size() && runTime >= eliteTimes[m_NextElite])
    {
        if (SpawnElite(grid, player, hp, swarm))
        {
            ++m_NextElite;
            m_Event = "elite";
        }
    }

    if (m_DebugElite && SpawnElite(grid, player, hp, swarm))
    {
        m_DebugElite = false;
        m_Event = "elite (button)";
    }

    // ---- Boss（面板のボタンは門を使ったのと同じ）----
    if (m_DebugBoss)
    {
        m_DebugBoss = false;
        if (m_Boss == BossState::None || m_Boss == BossState::Defeated)
        {
            m_BossSpawnHp = bossHp * mobs.GetStatMul();
            m_SummonPos = player;
            if (SpawnBoss(grid, player, swarm)) { m_Boss = BossState::Summoned; m_BossWait = 0.0f; m_Event = "boss (button)"; }
        }
    }

    // 生死は GPU の回読で見る（2〜3 フレーム遅れる）
    const Swarm::BossInfo& info = swarm.GetBossInfo();
    switch (m_Boss)
    {
    case BossState::Summoned:
        if (info.alive > 0)
        {
            m_Boss = BossState::Alive;
        }
        else if ((m_BossWait += dt) > 3.0f)
        {
            // 3 秒経っても現れない（湧く枠が無い等）: もう一度
            SpawnBoss(grid, m_SummonPos, swarm);
            m_BossWait = 0.0f;
        }
        break;
    case BossState::Alive:
        if (info.alive == 0)
        {
            m_Boss = BossState::Defeated;
            m_BossHpRatio = 0.0f;
            m_Event = "boss defeated";
            std::cout << "[Stage] boss defeated" << std::endl;
        }
        else
        {
            const float maxHp = Swarm::HpFromFixed(info.maxHp);
            const float cur = (info.hp > info.maxHp) ? 0.0f : Swarm::HpFromFixed(info.hp);   // 止めの一撃で折り返した値
            m_BossHpRatio = (maxHp > 0.0f) ? cur / maxHp : 0.0f;
            m_BossPos = { info.pos[0], info.pos[1], info.pos[2] };
        }
        break;
    default:
        break;
    }
}

// ============================================================
// 門を置く（開局・地形の作り直し）
// 乱数は地形の seed から（同じ seed なら同じ場所）。周り 3x3 も歩けて平らな所
// ============================================================
void StageDirector::SpawnPortal(Registry& reg, const GridWorld& grid, const Vector3& center, uint32_t seed,
    InteractionSystem& interaction, const Vector3* preferred, const Vector3* faceToward)
{
    if (m_Portal != EntityTraits::NULL_ENTITY && reg.IsValid(m_Portal)) reg.Destroy(m_Portal);
    m_Portal = EntityTraits::NULL_ENTITY;
    for (Entity e : m_PortalPillars)
        if (reg.IsValid(e)) reg.Destroy(e);
    m_PortalPillars.clear();
    interaction.ClearFocus();
    if (!m_PortalModel)
    {
        std::cout << "[Stage] portal model missing: " << Res::Mdl::Ruins_ArchGate << std::endl;
        return;
    }

    const Vector3 lo = m_PortalModel->GetBoundsMin();
    const Vector3 hi = m_PortalModel->GetBoundsMax();
    const float h = hi.y - lo.y;
    const float scale = (h > 1e-4f) ? portalHeight / h : 1.0f;
    const Vector3 face = faceToward ? *faceToward : center;

    std::mt19937 rng(seed * 104729u + 31u);
    std::uniform_real_distribution<float> angleDist(0.0f, DirectX::XM_2PI);
    std::uniform_real_distribution<float> radiusDist(portalMinDist, (std::max)(portalMinDist, portalMaxDist));

    // マス (gx, gz) の周り 3x3 が歩けて平らなら門を置いて true
    auto tryPlace = [&](int gx, int gz) -> bool
    {
        Vector3 pos = grid.CellToWorld(gx, gz);
        pos.y = grid.SampleHeight(pos.x, pos.z);
        bool ok = true;
        for (int dz = -1; dz <= 1 && ok; ++dz)
            for (int dx = -1; dx <= 1 && ok; ++dx)
            {
                if (!grid.IsWalkable(gx + dx, gz + dz)) { ok = false; break; }
                const Vector3 c = grid.CellToWorld(gx + dx, gz + dz);
                ok = std::fabs(grid.SampleHeight(c.x, c.z) - pos.y) < 0.2f;
            }
        if (!ok) return false;

        // 門の面（拱の局所 +Z）を face の方へ向ける（通り抜ける向きが近づく玩家から見える）
        const float yawDeg = DirectX::XMConvertToDegrees(std::atan2(face.x - pos.x, face.z - pos.z));
        const Matrix rot = Matrix::CreateRotationY(DirectX::XMConvertToRadians(yawDeg));
        // 包囲箱の xz の真ん中を pos に、底を地面に合わせる（模型の原点の位置に頼らない）
        const Vector3 midXZ((lo.x + hi.x) * 0.5f * scale, 0.0f, (lo.z + hi.z) * 0.5f * scale);

        Entity e = reg.Create();
        TransformComponent tf;
        tf.position = pos - Vector3::Transform(midXZ, rot) - Vector3(0.0f, lo.y * scale, 0.0f);
        tf.rotation = { 0.0f, yawDeg, 0.0f };
        tf.scale = { scale, scale, scale };
        reg.Add<TransformComponent>(e, tf);

        ModelComponent mc;
        mc.model = m_PortalModel;
        reg.Add<ModelComponent>(e, mc);

        // 拱の口はくぐれる。両脇の柱だけ玩家が抜けないように箱（雑魚は場面が下のマスを塞ぐ = BlockPropCells）。
        // 柱の太さは m で持つ（2026-10-03：以前は 0.3 × scale で、cm の FBX の倍率 0.016 を掛けて 5mm になっていた）
        const float halfW = (hi.x - lo.x) * 0.5f * scale;
        const float pillarHalf = 0.4f;   // 丸い柱の太さ ≈ 0.8m
        for (float side : { -1.0f, 1.0f })
        {
            const Vector3 c = pos + Vector3::Transform(Vector3(side * (halfW - pillarHalf), 0.0f, 0.0f), rot)
                + Vector3(0.0f, portalHeight * 0.5f, 0.0f);
            m_PortalPillars.push_back(TestSpawner::SpawnStaticBox(reg, c, { pillarHalf, portalHeight * 0.5f, pillarHalf }));
        }

        // 渦の中心 = 口の真ん中（丸い口の高さの 4 割ほど）
        m_PortalYaw = yawDeg;
        m_PortalCenter = pos + Vector3(0.0f, portalHeight * 0.43f, 0.0f);

        InteractableComponent it;
        it.kind = InteractKind::BossPortal;
        it.basePos = pos;
        it.animate = false;
        it.radius = 3.0f;
        it.prompt = L"[F] ボスを呼ぶ";
        it.lightColor = { 0.75f, 0.35f, 1.0f };   // 紫（渦の特効にも光がある。こちらは控えめ）
        it.lightRadius = 7.0f;
        it.lightIntensity = 1.0f;
        it.lightHeight = portalHeight * 0.43f;
        reg.Add<InteractableComponent>(e, it);

        m_Portal = e;
        std::cout << "[Stage] boss portal at " << pos.x << ", " << pos.y << ", " << pos.z
            << " (" << (pos - center).Length() << " m from the start)" << std::endl;
        return true;
    };

    // 指定の場所の近く：10 マス以内のマスを近い順に
    if (preferred)
    {
        int px = 0, pz = 0;
        grid.WorldToCell(*preferred, px, pz);
        std::vector<std::pair<int, int>> ring;   // (距離², マスの番号)
        constexpr int kSearch = 10;
        for (int dz = -kSearch; dz <= kSearch; ++dz)
            for (int dx = -kSearch; dx <= kSearch; ++dx)
            {
                const int gx = px + dx, gz = pz + dz;
                if (gx < 0 || gz < 0 || gx >= grid.Width() || gz >= grid.Depth()) continue;
                ring.push_back({ dx * dx + dz * dz, gz * grid.Width() + gx });
            }
        std::sort(ring.begin(), ring.end());
        for (const auto& c : ring)
            if (tryPlace(c.second % grid.Width(), c.second / grid.Width())) return;
    }

    for (int attempt = 0; attempt < 400; ++attempt)
    {
        const float a = angleDist(rng);
        const float r = radiusDist(rng);
        const Vector3 probe = center + Vector3(std::cos(a) * r, 0.0f, std::sin(a) * r);
        int gx = 0, gz = 0;
        grid.WorldToCell(probe, gx, gz);
        if (tryPlace(gx, gz)) return;
    }
    std::cout << "[Stage] no place for the boss portal" << std::endl;
}

bool StageDirector::TryUsePortal(Registry& reg, Entity used, const GridWorld& grid, const Vector3& player,
    const MobSpawner& mobs, SwarmSystem& swarm, InteractionSystem& interaction)
{
    if (used == EntityTraits::NULL_ENTITY || used != m_Portal || !reg.IsValid(used)) return false;
    if (!reg.Has<InteractableComponent>(used)) return false;
    if (reg.Get<InteractableComponent>(used).kind != InteractKind::BossPortal) return false;
    if (m_Boss != BossState::None) return false;

    // HP は呼んだ時の難度で決まる（遅いほど固い。Megabonk と同じ）
    m_BossSpawnHp = bossHp * mobs.GetStatMul();
    m_SummonPos = player;
    SpawnBoss(grid, player, swarm);   // 失敗しても Summoned のまま Update が湧かせ直す
    m_Boss = BossState::Summoned;
    m_BossWait = 0.0f;
    m_Event = "boss summoned";

    // 門は使えなくする（模型は残す。光と画面外の目印は消える）
    reg.Remove<InteractableComponent>(used);
    interaction.ClearFocus();
    return true;
}

// ============================================================
// ImGui: Enemies 面板の「Stage」の段
// ============================================================
void StageDirector::DrawImGui(SwarmSystem& swarm, float runTime)
{
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.9f, 1), "Stage");
    const int rem = (int)Remaining(runTime);
    ImGui::Text("time left %02d:%02d   elites spawned %d (next #%zu)",
        rem / 60, rem % 60, m_ElitesSpawned, m_NextElite);
    ImGui::DragFloat("Stage Time (s)", &stageTime, 1.0f, 10.0f, 3600.0f);
    ImGui::DragFloat("Elite HP (base)", &eliteHp, 5.0f, 1.0f, 100000.0f);
    ImGui::DragFloat("Elite Speed", &eliteSpeed, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloatRange2("Elite Ring", &eliteRingMin, &eliteRingMax, 0.5f, 5.0f, 60.0f);

    auto& kind = swarm.GetBomberParams();   // 精英・Boss の体格などは GPU の定数（BomberCB と相乗り）
    ImGui::DragFloat("Elite Scale", &kind.eliteScale, 0.05f, 1.0f, 5.0f);
    ImGui::DragFloat("Elite Damage x", &kind.eliteDamageMul, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("Elite Exp x", &kind.eliteExpMul, 0.5f, 0.0f, 200.0f);
    if (ImGui::Button("Spawn Elite Now")) m_DebugElite = true;

    ImGui::Text("final swarm level %d", m_FinalLevel);
    ImGui::DragFloat("Final Spawn / s", &finalSpawnRate, 0.5f, 0.0f, 200.0f);
    ImGui::DragFloat("Final Speed x", &finalSpeedMul, 0.01f, 0.1f, 5.0f);
    ImGui::DragFloat("Final Step (s)", &finalStepTime, 1.0f, 1.0f, 300.0f);
    ImGui::DragFloat("Final Step Stat x", &finalStepMul, 0.01f, 1.0f, 5.0f);
    ImGui::DragInt("Final Cap (first 2 min)", &finalCap, 1, 0, 4096);
    ImGui::DragInt("Final Cap (later)", &finalCapLate, 1, 0, 4096);
    ImGui::DragFloat("Final Ghosts / s", &finalGhostRate, 0.1f, 0.0f, 50.0f);
    ImGui::DragFloat("Final Ghost Step x", &finalGhostStepMul, 0.01f, 1.0f, 5.0f);
    ImGui::DragFloat("Ghost Hover", &kind.ghostHover, 0.05f, 0.0f, 3.0f);
    ImGui::DragFloat("Ghost Alpha", &kind.ghostAlpha, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Ghost Glow", &kind.ghostGlow, 0.05f, 0.0f, 6.0f);
    ImGui::DragFloat("Ghost Damage x", &kind.ghostDamageMul, 0.05f, 0.0f, 20.0f);

    static const char* kBossNames[] = { "none", "summoned", "alive", "defeated" };
    const Swarm::BossInfo& info = swarm.GetBossInfo();
    ImGui::Text("boss %s  hp %.0f / %.0f  (gpu alive %u)", kBossNames[(int)m_Boss],
        Swarm::HpFromFixed(info.hp > info.maxHp ? 0u : info.hp), Swarm::HpFromFixed(info.maxHp), info.alive);
    ImGui::DragFloat("Boss HP (base)", &bossHp, 50.0f, 1.0f, 1000000.0f);
    ImGui::DragFloat("Boss Speed", &bossSpeed, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Boss Scale", &kind.bossScale, 0.05f, 1.0f, 10.0f);
    ImGui::DragFloat("Boss Damage x", &kind.bossDamageMul, 0.05f, 0.0f, 20.0f);
    if (ImGui::Button("Summon Boss Now")) m_DebugBoss = true;
}
