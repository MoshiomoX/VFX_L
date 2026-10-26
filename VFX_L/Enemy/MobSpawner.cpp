// ============================================================
// MobSpawner.cpp
// ============================================================
#include "Enemy/MobSpawner.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/AreaProfile.h"
#include "World/GridWorld.h"
#include "Audio/AudioSystem.h"
#include "imgui.h"

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
    Vector3 p = { pos.x, pos.y + groundY, pos.z };
    // 1 回の乱数を種類の割合で区切る（どれも雑魚の枠と入れ替わる = 湧く数は変えない）。
    // 2026-10-25 に if の連なりから表にした（遺跡の敵が増えて長くなった）
    struct Pick { float ratio; uint32_t kind; float hp, speed; };
    const Pick picks[] = {
        { m_BomberRatio, Swarm::kEnemyKindBomber, m_BomberHp, m_BomberSpeed },
        { m_SplitterRatio, Swarm::kEnemyKindSplitter, m_SplitterHp, m_SplitterSpeed },
        { m_BruteRatio, Swarm::kEnemyKindBrute, m_BruteHp, m_BruteSpeed },
        { m_ChargerRatio, Swarm::kEnemyKindCharger, m_ChargerHp, m_ChargerSpeed },
        { m_ShieldRatio, Swarm::kEnemyKindShield, m_ShieldHp, m_ShieldSpeed },
        { m_SpikeBallRatio, Swarm::kEnemyKindSpikeBall, m_SpikeBallHp, m_SpikeBallSpeed },     // トゲ玉（砂漠。2026-10-17）
        { m_BurrowerRatio, Swarm::kEnemyKindBurrower, m_BurrowerHp, m_BurrowerSpeed },         // 砂潜り（砂漠。2026-10-17）
        { m_SandMageRatio, Swarm::kEnemyKindSandMage, m_SandMageHp, m_SandMageSpeed },         // 砂嵐の術師（砂漠。2026-10-17）
        { m_DashBomberRatio, Swarm::kEnemyKindDashBomber, m_DashBomberHp, m_DashBomberSpeed }, // 突撃爆弾兵（全ステージ。2026-10-18）
        { m_ArmorRatio, Swarm::kEnemyKindArmor, m_ArmorHp, m_ArmorSpeed },                     // 遺跡の敵（2026-10-25）
        { m_GargoyleRatio, Swarm::kEnemyKindGargoyle, m_GargoyleHp, m_GargoyleSpeed },
        { m_NecroRatio, Swarm::kEnemyKindNecro, m_NecroHp, m_NecroSpeed },
        { m_WardenRatio, Swarm::kEnemyKindWarden, m_WardenHp, m_WardenSpeed },
    };
    const float roll = Rand01();
    uint32_t kind = Swarm::kEnemyKindMob;
    float hp = m_MobHp, speed = m_MobSpeed;
    float acc = 0.0f;
    for (const Pick& k : picks)
    {
        acc += k.ratio;
        if (roll < acc)
        {
            kind = k.kind;
            hp = k.hp;
            speed = k.speed;
            break;
        }
    }
    hp *= m_HpMul;   // 湧いた瞬間の難度で決まる
    speed *= finalSpeedMul;

    float yaw = 0.0f;
    if (kind == Swarm::kEnemyKindGargoyle)
    {
        // 石像はプレイヤーの近くに（遠くの輪では目覚めるところが見えない）。プレイヤーの方を向いて眠る
        p = GargoyleSpot(p, groundY);
        yaw = std::atan2(m_Player.x - p.x, m_Player.z - p.z);
    }
    if (recycle) swarm.RecycleEnemy(p, hp, speed, kind);
    else         swarm.SpawnEnemy(p, hp, speed, kind, yaw);
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
// GPU の分裂の環（2〜3 フレーム遅れ）に届いた出来事：
//   ・スプリッターの死 → 分裂体を死んだ所の周りに。同時上限（spawnCap）は見ない：倒した結果なので必ず出す
//     （池が満杯なら GPU が捨てる）。周りの点が塞がったマスなら死んだ所そのものに置く（GPU が重なりを押し広げる）
//   ・砂嵐の術師の詠唱（kind = kEnemyKindSandMage。2026-10-17）→ 術師の 1m 前から今のプレイヤーへ向けて砂旋
//     （kEnemyKindTornado）を 1 つ。向きは依頼の yaw に乗せる（AICS はその向きへ moveSpeed で飛ばす）。
//     HP は撃てないので形だけ。同時上限は見ない（術師の攻撃なので必ず出る）
// ============================================================
void MobSpawner::SpawnFromEvents(const GridWorld& grid, const Vector3& player, SwarmSystem& swarm)
{
    static std::vector<Swarm::SplitEvent> s_Events;
    s_Events.clear();
    swarm.ConsumeSplitEvents(s_Events);
    if (s_Events.empty()) return;

    const float hp = m_SplitlingHp * m_HpMul;
    const float speed = m_MobSpeed * m_SplitlingSpeedMul * finalSpeedMul;
    uint32_t splits = 0;
    for (const Swarm::SplitEvent& ev : s_Events)
    {
        const uint32_t evKind = ev.kind & Swarm::kSplitKindMask;   // 上 16 bit は出来事の中身（2026-10-25）
        if (HandleRuinsEvent(ev, grid, player, swarm))
            continue;   // 遺跡の敵（MobSpawnerRuins.cpp）
        if (evKind == Swarm::kEnemyKindSandMage)
        {
            Vector3 dir = player - ev.position;
            dir.y = 0.0f;
            if (dir.LengthSquared() < 1e-4f) dir = Vector3(0.0f, 0.0f, 1.0f);
            dir.Normalize();
            const float yaw = std::atan2(dir.x, dir.z);
            const Vector3 p = ev.position + dir * 1.0f;
            swarm.SpawnEnemy(p, 1000000.0f, tornadoSpeed * finalSpeedMul, Swarm::kEnemyKindTornado, yaw);
            ++m_TornadoesSpawned;
            m_TornadoBirths.push_back({ p, yaw, tornadoSpeed * finalSpeedMul });   // 見た目（SandTornadoFx）用
            AudioSystem::Get().Play("cast_gust");
            continue;
        }
        if (evKind != Swarm::kEnemyKindSplitter)
            continue;
        ++m_SplitEventsSeen;
        ++splits;
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
    if (splits > 0)
        AudioSystem::Get().PlayBurst("enemy_split", splits);
}

void MobSpawner::Update(const GridWorld& grid, const Vector3& player, float dt, float runTime, SwarmSystem& swarm)
{
    swarm.SetRecycleMinDist(m_Director.rMax);
    m_Grid = &grid;       // ガーゴイルの石像を置く所を探す（Request の中）
    m_Player = player;

    // ---- 難度：経過時間 → 強さの倍率と湧く速さ ----
    m_RunTime = runTime;
    if (scaling)
    {
        const DifficultyCurve::Sample s = curve.Evaluate(runTime / 60.0f + stageTimeOffsetMin);   // 面毎に表を進めて読む（2026-10-17）
        m_HpMul = s.hpMul * stageHpMul * finalStatMul;
        m_DamageMul = s.damageMul * stageDamageMul * finalStatMul;
        m_Director.spawnPerSecond = ((finalSpawnRate > 0.0f) ? finalSpawnRate : s.spawnRate) * stageSpawnMul * waveRateMul;
    }
    else
        m_HpMul = m_DamageMul = 1.0f;
    // スプリッター（面毎の表）・重装兵（全ステージ共通）の割合。他の種類と同じ KindRamp の式
    //（start 前は 0、rampEnd まで直線）。値はシーンが入れる splitter* / brute* のまま
    m_SplitterRatio = KindRamp{ splitterStart, splitterRatioStart, splitterRatioEnd, splitterRampEnd }.At(runTime);
    m_BruteRatio = KindRamp{ bruteStart, bruteRatioStart, bruteRatioEnd, bruteRampEnd }.At(runTime);
    // 突撃兵・盾兵（面毎の表）
    m_ChargerRatio = chargerMix.At(runTime);
    m_ShieldRatio = shieldMix.At(runTime);
    m_SpikeBallRatio = spikeBallMix.At(runTime);
    m_BurrowerRatio = burrowerMix.At(runTime);
    m_SandMageRatio = sandMageMix.At(runTime);
    m_DashBomberRatio = dashBomberMix.At(runTime);
    m_ArmorRatio = armorMix.At(runTime);         // 遺跡の敵（2026-10-25）
    m_GargoyleRatio = gargoyleMix.At(runTime);
    m_NecroRatio = necroMix.At(runTime);
    m_WardenRatio = wardenMix.At(runTime);
    swarm.GetAIParams().contactDamage = baseContactDamage * m_DamageMul;
    swarm.GetBomberParams().blastDamage = baseBlastDamage * m_DamageMul;

    // 最終ウェーブ中は幽霊の枠（ghostCap + darkGhostCap）を雑魚の上限から引いておく：雑魚には「幽霊の枠は常に埋まっている」
    // ように見せる（2026-10-18。以前は共通の上限を雑魚が先に埋めて幽霊がほぼ湧かなかった）。幽霊自身は SpawnGhosts で自分の上限を見る
    int aliveForMobs = (int)swarm.GetCounters().aliveEnemies;
    if (finalGhostRate > 0.0f || finalDarkGhostRate > 0.0f)
    {
        const auto& info = swarm.GetBossInfo();
        aliveForMobs = aliveForMobs - (int)info.ghosts - (int)info.darkGhosts + ghostCap + darkGhostCap;
    }
    m_Director.Update(grid, player, dt,
        aliveForMobs,
        [this, &swarm](const Vector3& pos) { Request(swarm, pos, false); },
        [this, &swarm](const Vector3& pos) { Request(swarm, pos, true); });

    SpawnDebugKind(grid, player, swarm, m_DebugBombers, m_BomberHp * m_HpMul, m_BomberSpeed, Swarm::kEnemyKindBomber);   // 他の種類と同じく今の HP 倍率を掛ける
    SpawnDebugKind(grid, player, swarm, m_DebugSplitters, m_SplitterHp * m_HpMul, m_SplitterSpeed, Swarm::kEnemyKindSplitter);
    SpawnDebugKind(grid, player, swarm, m_DebugBrutes, m_BruteHp * m_HpMul, m_BruteSpeed, Swarm::kEnemyKindBrute);
    SpawnDebugKind(grid, player, swarm, m_DebugChargers, m_ChargerHp * m_HpMul, m_ChargerSpeed, Swarm::kEnemyKindCharger);
    SpawnDebugKind(grid, player, swarm, m_DebugShields, m_ShieldHp * m_HpMul, m_ShieldSpeed, Swarm::kEnemyKindShield);
    SpawnDebugKind(grid, player, swarm, m_DebugSpikeBalls, m_SpikeBallHp * m_HpMul, m_SpikeBallSpeed, Swarm::kEnemyKindSpikeBall);
    SpawnDebugKind(grid, player, swarm, m_DebugBurrowers, m_BurrowerHp * m_HpMul, m_BurrowerSpeed, Swarm::kEnemyKindBurrower);
    SpawnDebugKind(grid, player, swarm, m_DebugSandMages, m_SandMageHp * m_HpMul, m_SandMageSpeed, Swarm::kEnemyKindSandMage);
    SpawnDebugKind(grid, player, swarm, m_DebugDashBombers, m_DashBomberHp * m_HpMul, m_DashBomberSpeed, Swarm::kEnemyKindDashBomber);
    SpawnFromEvents(grid, player, swarm);
    UpdateRuins(grid, player, dt, swarm);   // 鎧の蘇り・骸骨の起き上がり・パネルのボタンの分（MobSpawnerRuins.cpp）
    SpawnGhosts(player, dt, swarm);
}

// ============================================================
// 最終ウェーブの幽霊（白 / 黒）。プレイヤーの周りの環（rMin〜rMax）に湧く。壁を素通りするので歩けるマスかは見ない。
// 同時上限は種類毎（ghostCap / darkGhostCap。数は CompactCS が BossInfo に数えた物、2〜3 フレーム遅れ）。
// 雑魚の上限とは別（2026-10-18。Update で雑魚側からこの枠を引いている）。池が一杯なら湧かない
// ============================================================
void MobSpawner::SpawnGhosts(const Vector3& player, float dt, SwarmSystem& swarm)
{
    const auto& info = swarm.GetBossInfo();
    const int alive = (int)swarm.GetCounters().aliveEnemies;
    auto budget = [&](float rate, float& accum, int cap, uint32_t now, int debug)
    {
        int want = debug;
        if (rate > 0.0f && m_Director.enabled)
        {
            accum += rate * dt;
            const int n = (int)accum;
            accum -= (float)n;
            if ((int)now < cap && alive < (int)Swarm::kMaxEnemies - 64) want += n;
        }
        else
            accum = 0.0f;
        return (std::min)(want, m_Director.maxPerFrame);
    };
    const int white = budget(finalGhostRate, m_GhostAccum, ghostCap, info.ghosts, m_DebugGhosts);
    const int dark = budget(finalDarkGhostRate, m_DarkGhostAccum, darkGhostCap, info.darkGhosts, m_DebugDarkGhosts);
    m_DebugGhosts = m_DebugDarkGhosts = 0;

    const float groundY = swarm.GetAIParams().groundY;
    auto ring = [&]()
    {
        const float ang = Rand01() * 6.2831853f;
        const float r = m_Director.rMin + (m_Director.rMax - m_Director.rMin) * Rand01();
        return Vector3(player.x + std::cos(ang) * r, groundY, player.z + std::sin(ang) * r);
    };
    for (int k = 0; k < white; ++k)
    {
        swarm.SpawnEnemy(ring(), m_MobHp * ghostHpMul * m_HpMul, m_MobSpeed * ghostSpeedMul, Swarm::kEnemyKindGhost);
        ++m_GhostsSpawned;
    }
    for (int k = 0; k < dark; ++k)
    {
        swarm.SpawnEnemy(ring(), m_MobHp * darkGhostHpMul * m_HpMul, m_MobSpeed * darkGhostSpeedMul, Swarm::kEnemyKindDarkGhost);
        ++m_DarkGhostsSpawned;
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
    ImGui::Text("stage: table +%.0f min  HP x%.2f  damage x%.2f  spawn x%.2f   final x%.2f", stageTimeOffsetMin, stageHpMul, stageDamageMul, stageSpawnMul, finalStatMul);
    curve.DrawImGui(m_RunTime / 60.0f + stageTimeOffsetMin, stageTimeOffsetMin);   // 表（分・湧き・HP・ダメージ）と曲線（この面の分）、Save / Load / Reset
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
    ImGui::Separator();

    // ---- 最終ウェーブの幽霊（白 / 黒）。湧く速さは Stage の段（StageDirector）、ここは個体の値と上限 ----
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1), "Ghosts (final wave, GPU)");
    {
        const auto& info = swarm.GetBossInfo();
        ImGui::Text("white %u / %d   dark %u / %d   (spawned %u / %u)   rate %.2f / %.2f per s",
            info.ghosts, ghostCap, info.darkGhosts, darkGhostCap, m_GhostsSpawned, m_DarkGhostsSpawned, finalGhostRate, finalDarkGhostRate);
    }
    ImGui::DragFloat("Ghost Speed x", &ghostSpeedMul, 0.05f, 1.0f, 6.0f);
    ImGui::DragFloat("Ghost HP x", &ghostHpMul, 0.5f, 0.1f, 200.0f);
    ImGui::DragInt("Ghost Cap", &ghostCap, 1, 0, 2000);
    ImGui::DragFloat("Dark Ghost Speed x", &darkGhostSpeedMul, 0.05f, 1.0f, 6.0f);
    ImGui::DragFloat("Dark Ghost HP x", &darkGhostHpMul, 1.0f, 0.1f, 1000.0f);
    ImGui::DragInt("Dark Ghost Cap", &darkGhostCap, 1, 0, 500);
    {
        auto& kind = swarm.GetBomberParams();
        ImGui::DragFloat("Dark Ghost Scale", &kind.darkGhostScale, 0.05f, 0.5f, 4.0f);
        ImGui::DragFloat("Dark Ghost Damage x", &kind.darkGhostDamageMul, 0.1f, 0.0f, 20.0f);
        ImGui::DragFloat("Dark Ghost Exp x", &kind.darkGhostExpMul, 0.5f, 0.0f, 200.0f);
        ImGui::DragFloat("Dark Ghost Alpha", &kind.darkGhostAlpha, 0.01f, 0.0f, 1.0f);
    }
    if (ImGui::Button("Spawn 5 Ghosts")) QueueDebugGhosts(5);
    ImGui::SameLine();
    if (ImGui::Button("Spawn 5 Dark Ghosts")) QueueDebugDarkGhosts(5);
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

    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.6f, 1), "Brute (GPU)");
    ImGui::Text("ratio now %.2f", m_BruteRatio);
    ImGui::DragFloat("Brute Start (s)", &bruteStart, 5.0f, 0.0f, 1.0e9f);
    ImGui::DragFloat("Brute Ratio Start", &bruteRatioStart, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Brute Ratio End", &bruteRatioEnd, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Brute Ramp End (s)", &bruteRampEnd, 5.0f, 0.0f, 3600.0f);
    ImGui::DragFloat("Brute HP", &m_BruteHp, 1.0f, 1.0f, 2000.0f);
    ImGui::DragFloat("Brute Speed", &m_BruteSpeed, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Brute Scale", &bomb.bruteScale, 0.01f, 0.2f, 3.0f);
    ImGui::DragFloat("Brute Damage x", &bomb.bruteDamageMul, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Brute Exp x", &bomb.bruteExpMul, 0.05f, 0.0f, 20.0f);
    if (ImGui::Button("Spawn 5 Brutes Nearby")) QueueDebugBrutes(5);
    ImGui::Separator();

    DrawChargerShieldImGui(swarm);   // 突撃兵・盾兵・凍結（MobSpawnerKinds.cpp）
    DrawRuinsImGui(swarm);           // 遺跡の敵（MobSpawnerRuins.cpp）

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

// 種類毎の今の HP と速さ（トレーニングの「群れを出す」。普段の湧きと同じ値）
void MobSpawner::KindStats(uint32_t kind, float& hp, float& speed) const
{
    switch (kind)
    {
    case Swarm::kEnemyKindBomber:    hp = m_BomberHp;    speed = m_BomberSpeed; break;
    case Swarm::kEnemyKindSplitter:  hp = m_SplitterHp;  speed = m_SplitterSpeed; break;
    case Swarm::kEnemyKindSplitling: hp = m_SplitlingHp; speed = m_MobSpeed * m_SplitlingSpeedMul; break;
    case Swarm::kEnemyKindBrute:     hp = m_BruteHp;     speed = m_BruteSpeed; break;
    case Swarm::kEnemyKindCharger:   hp = m_ChargerHp;   speed = m_ChargerSpeed; break;
    case Swarm::kEnemyKindShield:    hp = m_ShieldHp;    speed = m_ShieldSpeed; break;
    case Swarm::kEnemyKindSpikeBall: hp = m_SpikeBallHp; speed = m_SpikeBallSpeed; break;
    case Swarm::kEnemyKindBurrower:  hp = m_BurrowerHp;  speed = m_BurrowerSpeed; break;
    case Swarm::kEnemyKindSandMage:  hp = m_SandMageHp;  speed = m_SandMageSpeed; break;
    case Swarm::kEnemyKindDashBomber: hp = m_DashBomberHp; speed = m_DashBomberSpeed; break;
    case Swarm::kEnemyKindGhost:     hp = m_MobHp * ghostHpMul;     speed = m_MobSpeed * ghostSpeedMul; break;
    case Swarm::kEnemyKindDarkGhost: hp = m_MobHp * darkGhostHpMul; speed = m_MobSpeed * darkGhostSpeedMul; break;
    case Swarm::kEnemyKindArmor:     hp = m_ArmorHp;     speed = m_ArmorSpeed; break;      // 遺跡の敵（2026-10-25）
    case Swarm::kEnemyKindArmorRevived: hp = m_ArmorHp * armorReviveHpFrac; speed = m_ArmorSpeed; break;
    case Swarm::kEnemyKindGargoyle:  hp = m_GargoyleHp;  speed = m_GargoyleSpeed; break;
    case Swarm::kEnemyKindNecro:     hp = m_NecroHp;     speed = m_NecroSpeed; break;
    case Swarm::kEnemyKindSkeleton:  hp = m_SkeletonHp;  speed = m_SkeletonSpeed; break;
    case Swarm::kEnemyKindWarden:    hp = m_WardenHp;    speed = m_WardenSpeed; break;
    default:                         hp = m_MobHp;       speed = m_MobSpeed; break;
    }
    hp *= m_HpMul;
}
