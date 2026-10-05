// ============================================================
// MobSpawner.cpp
// ============================================================
#include "Enemy/MobSpawner.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/AreaProfile.h"
#include "World/GridWorld.h"
#include "Audio/AudioSystem.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

using DirectX::SimpleMath::Vector3;

namespace
{
    // パネルのボタンで湧かせる自爆兵の輪（プレイヤーから m）
    constexpr float kDebugRingMin = 8.0f;
    constexpr float kDebugRingMax = 12.0f;

    float Rand01() { return (float)rand() / RAND_MAX; }
}

void MobSpawner::Init(SwarmSystem& swarm)
{
    swarm.SetRecycleMinDist(m_Director.rMax);

    // 爆発の見た目の範囲。AreaProfileDB::LoadAll の後に呼ばれる（無ければ 0 = 見た目無しで爆発だけ）
    swarm.GetBomberParams().blastArea = (uint32_t)AreaProfileDB::IndexOf("BomberBlast");
    // 死んだ敵の足元の土煙（砕け散りと一緒に出る。無ければ 0 = 部品が飛ぶだけ）
    swarm.corpse.deathArea = (uint32_t)AreaProfileDB::IndexOf("MobDeath");

    // 難度の倍率を掛ける元
    baseContactDamage = swarm.GetAIParams().contactDamage;
    baseBlastDamage = swarm.GetBomberParams().blastDamage;

    // 難度の表（保存してあれば。無ければコードの既定値）
    curve.Load();
}

void MobSpawner::Request(SwarmSystem& swarm, const Vector3& pos, bool recycle)
{
    // pos.y = 地面の高さ（SpawnDirector が高さ場から引く）
    const float groundY = swarm.GetAIParams().groundY;
    const Vector3 p = { pos.x, pos.y + groundY, pos.z };
    const float roll = Rand01();
    uint32_t kind = Swarm::kEnemyKindMob;
    float hp = m_MobHp, speed = m_MobSpeed;
    if (roll < m_BomberRatio)
    {
        kind = Swarm::kEnemyKindBomber;
        hp = m_BomberHp;
        speed = m_BomberSpeed;
    }
    else if (roll < m_BomberRatio + m_SplitterRatio)
    {
        kind = Swarm::kEnemyKindSplitter;
        hp = m_SplitterHp;
        speed = m_SplitterSpeed;
    }
    hp *= m_HpMul;   // 湧いた瞬間の難度で決まる
    speed *= finalSpeedMul;

    if (recycle) swarm.RecycleEnemy(p, hp, speed, kind);
    else         swarm.SpawnEnemy(p, hp, speed, kind);
}

void MobSpawner::SpawnDebugKind(const GridWorld& grid, const Vector3& player, SwarmSystem& swarm,
    int& count, float hp, float speed, uint32_t kind)
{
    const float groundY = swarm.GetAIParams().groundY;
    for (; count > 0; --count)
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const float ang = Rand01() * 6.2831853f;
            const float r = kDebugRingMin + (kDebugRingMax - kDebugRingMin) * Rand01();
            const Vector3 pos = player + Vector3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);

            int gx = 0, gz = 0;
            grid.WorldToCell(pos, gx, gz);
            if (!grid.IsWalkable(gx, gz)) continue;

            swarm.SpawnEnemy({ pos.x, grid.SampleHeight(pos.x, pos.z) + groundY, pos.z }, hp, speed, kind);
            break;
        }
    }
}

// ============================================================
// スプリッターの死（GPU の分裂の環、2〜3 フレーム遅れ）→ 分裂体を死んだ所の周りに。
// 同時上限（spawnCap）は見ない：倒した結果なので必ず出す（池が満杯なら GPU が捨てる）。
// 周りの点が塞がったマスなら死んだ所そのものに置く（GPU が重なりを押し広げる）
// ============================================================
void MobSpawner::SpawnSplitlings(const GridWorld& grid, SwarmSystem& swarm)
{
    static std::vector<Swarm::SplitEvent> s_Events;
    s_Events.clear();
    swarm.ConsumeSplitEvents(s_Events);
    if (s_Events.empty()) return;

    const float hp = m_SplitlingHp * m_HpMul;
    const float speed = m_MobSpeed * m_SplitlingSpeedMul * finalSpeedMul;
    for (const Swarm::SplitEvent& ev : s_Events)
    {
        ++m_SplitEventsSeen;
        const float base = Rand01() * 6.2831853f;
        for (int k = 0; k < m_SplitCount; ++k)
        {
            const float ang = base + 6.2831853f * (float)k / (float)(std::max)(1, m_SplitCount);
            Vector3 p = ev.position + Vector3(std::cos(ang), 0.0f, std::sin(ang)) * m_SplitSpread;
            int gx = 0, gz = 0;
            grid.WorldToCell(p, gx, gz);
            if (!grid.IsWalkable(gx, gz)) p = ev.position;
            swarm.SpawnEnemy(p, hp, speed, Swarm::kEnemyKindSplitling);
            ++m_SplitlingsSpawned;
        }
    }
    AudioSystem::Get().PlayBurst("enemy_split", (uint32_t)s_Events.size());
}

void MobSpawner::Update(const GridWorld& grid, const Vector3& player, float dt, float runTime, SwarmSystem& swarm)
{
    swarm.SetRecycleMinDist(m_Director.rMax);

    // ---- 難度：経過時間 → 強さの倍率と湧く速さ ----
    m_RunTime = runTime;
    if (scaling)
    {
        const DifficultyCurve::Sample s = curve.Evaluate(runTime / 60.0f);
        m_HpMul = (s.hpMul + statMulBonus) * finalStatMul;
        m_DamageMul = (s.damageMul + statMulBonus) * finalStatMul;
        m_Director.spawnPerSecond = (finalSpawnRate > 0.0f) ? finalSpawnRate : s.spawnRate;
    }
    else
        m_HpMul = m_DamageMul = 1.0f;
    // スプリッターの割合（面毎の表。start 前は 0、rampEnd まで直線）
    if (runTime < splitterStart)
        m_SplitterRatio = 0.0f;
    else
    {
        const float span = (std::max)(1.0f, splitterRampEnd - splitterStart);
        const float t = (std::min)(1.0f, (runTime - splitterStart) / span);
        m_SplitterRatio = splitterRatioStart + (splitterRatioEnd - splitterRatioStart) * t;
    }
    swarm.GetAIParams().contactDamage = baseContactDamage * m_DamageMul;
    swarm.GetBomberParams().blastDamage = baseBlastDamage * m_DamageMul;

    m_Director.Update(grid, player, dt,
        (int)swarm.GetCounters().aliveEnemies,
        [this, &swarm](const Vector3& pos) { Request(swarm, pos, false); },
        [this, &swarm](const Vector3& pos) { Request(swarm, pos, true); });

    SpawnDebugKind(grid, player, swarm, m_DebugBombers, m_BomberHp, m_BomberSpeed, Swarm::kEnemyKindBomber);
    SpawnDebugKind(grid, player, swarm, m_DebugSplitters, m_SplitterHp * m_HpMul, m_SplitterSpeed, Swarm::kEnemyKindSplitter);
    SpawnSplitlings(grid, swarm);
    SpawnGhosts(player, dt, swarm);
}

// ============================================================
// 最終ウェーブの幽霊。プレイヤーの周りの環（rMin〜rMax）に湧く。壁を素通りするので歩けるマスかは見ない。
// 同時上限は雑魚と共通（Director.spawnCap。alive は種類を問わず数える）
// ============================================================
void MobSpawner::SpawnGhosts(const Vector3& player, float dt, SwarmSystem& swarm)
{
    int want = m_DebugGhosts;
    m_DebugGhosts = 0;
    if (finalGhostRate > 0.0f && m_Director.enabled)
    {
        m_GhostAccum += finalGhostRate * dt;
        const int n = (int)m_GhostAccum;
        m_GhostAccum -= (float)n;
        if ((int)swarm.GetCounters().aliveEnemies < m_Director.spawnCap) want += n;
    }
    else
        m_GhostAccum = 0.0f;

    const float groundY = swarm.GetAIParams().groundY;
    for (int k = 0; k < want && k < m_Director.maxPerFrame; ++k)
    {
        const float ang = Rand01() * 6.2831853f;
        const float r = m_Director.rMin + (m_Director.rMax - m_Director.rMin) * Rand01();
        const Vector3 pos = player + Vector3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);
        swarm.SpawnEnemy({ pos.x, groundY, pos.z }, m_MobHp * m_HpMul, m_MobSpeed * ghostSpeedMul, Swarm::kEnemyKindGhost);
    }
}

// ============================================================
// ImGui: Enemies パネルの雑魚の段
// ============================================================
void MobSpawner::DrawImGui(SwarmSystem& swarm)
{
    // ---- 湧き管理 ----
    ImGui::Checkbox("Director Enabled", &m_Director.enabled);
    ImGui::Text("Mobs : %d / %d   (spawned %d, recycled %d)",
        m_Director.GetLastMobCount(), m_Director.spawnCap,
        m_Director.GetTotalSpawned(), m_Director.GetTotalRecycled());
    ImGui::DragInt("Spawn Cap", &m_Director.spawnCap, 1, 0, 4096);
    ImGui::BeginDisabled(scaling);   // 難度が効いている間は毎フレーム上書きされる
    ImGui::DragFloat("Spawn / s", &m_Director.spawnPerSecond, 0.05f, 0.0f, 200.0f);
    ImGui::EndDisabled();
    ImGui::DragInt("Max / Frame", &m_Director.maxPerFrame, 1, 1, 256);
    ImGui::DragFloat("Ring Min", &m_Director.rMin, 0.5f, 5.0f, 100.0f);
    ImGui::DragFloat("Ring Max", &m_Director.rMax, 0.5f, 5.0f, 120.0f);
    ImGui::TextDisabled("recycle: enemies farther than Ring Max get teleported");
    ImGui::Separator();

    // ---- 難度（経過時間で上がる）----
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1), "Difficulty");
    ImGui::Checkbox("Scale with time", &scaling);
    ImGui::Text("run %.0f s  HP x%.2f  damage x%.2f  spawn %.2f /s  contact %.1f  blast %.1f",
        m_RunTime, m_HpMul, m_DamageMul, m_Director.spawnPerSecond,
        swarm.GetAIParams().contactDamage, swarm.GetBomberParams().blastDamage);
    ImGui::Text("stage bonus +%.2f (HP & damage)   final x%.2f", statMulBonus, finalStatMul);
    curve.DrawImGui(m_RunTime / 60.0f);   // 表（分・湧き・HP・ダメージ）と曲線、Save / Load / Reset
    ImGui::DragFloat("Contact Damage (base)", &baseContactDamage, 0.5f, 0.0f, 200.0f);
    ImGui::DragFloat("Blast Damage (base)", &baseBlastDamage, 0.5f, 0.0f, 400.0f);
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
    ImGui::SameLine();
    if (ImGui::Button("Spawn 5 Ghosts")) QueueDebugGhosts(5);
    ImGui::DragFloat("Ghost Speed x", &ghostSpeedMul, 0.05f, 1.0f, 6.0f);
    ImGui::Separator();

    // ---- スプリッター（湧きの割合は面の表 + 経過時間、初期値は CPU、体格・倍率は GPU の定数）----
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1), "Splitter (GPU)");
    ImGui::Text("ratio now %.2f   splits %u -> splitlings %u", m_SplitterRatio, m_SplitEventsSeen, m_SplitlingsSpawned);
    ImGui::DragFloat("Splitter Start (s)", &splitterStart, 5.0f, 0.0f, 1.0e9f);
    ImGui::DragFloat("Splitter Ratio Start", &splitterRatioStart, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Splitter Ratio End", &splitterRatioEnd, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Splitter Ramp End (s)", &splitterRampEnd, 5.0f, 0.0f, 3600.0f);
    ImGui::DragFloat("Splitter HP", &m_SplitterHp, 1.0f, 1.0f, 1000.0f);
    ImGui::DragFloat("Splitter Speed", &m_SplitterSpeed, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Splitter Scale", &bomb.splitterScale, 0.01f, 0.2f, 3.0f);
    ImGui::DragFloat("Splitter Damage x", &bomb.splitterDamageMul, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Splitter Exp x", &bomb.splitterExpMul, 0.05f, 0.0f, 20.0f);
    ImGui::DragInt("Split Count", &m_SplitCount, 1, 0, 8);
    ImGui::DragFloat("Split Spread (m)", &m_SplitSpread, 0.05f, 0.0f, 3.0f);
    ImGui::DragFloat("Splitling HP", &m_SplitlingHp, 0.5f, 1.0f, 500.0f);
    ImGui::DragFloat("Splitling Speed x", &m_SplitlingSpeedMul, 0.05f, 0.1f, 5.0f);
    ImGui::DragFloat("Splitling Scale", &bomb.splitlingScale, 0.01f, 0.2f, 2.0f);
    ImGui::DragFloat("Splitling Damage x", &bomb.splitlingDamageMul, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Splitling Exp x", &bomb.splitlingExpMul, 0.05f, 0.0f, 10.0f);
    if (ImGui::Button("Spawn 5 Splitters Nearby")) QueueDebugSplitters(5);
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

    ImGui::DragFloat("Attack Interval", &ai.attackInterval, 0.05f, 0.1f, 5.0f);
    ImGui::DragFloat("Hit Stun (s)", &ai.hitStun, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat("Hit Flash Gain", &ai.hitFlash, 0.1f, 1.0f, 10.0f);
}
