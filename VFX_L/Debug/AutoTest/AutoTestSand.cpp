// ============================================================
// AutoTestSand.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=sand（VFXL_STAGE=2 = 砂漠で）
// 砂漠の砂の面（World/SandField、2026-11-02）とオアシス：
//   1 秒   : 湧き停止・全消し・無敵・詠唱停止。オアシスの草のマス数と LOD の網の大きさを書く
//   1.5 秒 : 正面 7m の左に「熱い」、右に「濡れた」の円（パネルのボタンと同じ Stamp）
//   2.3 秒 : look heatwet（左に陽炎、右に暗い濡れた砂）、3 点の読み戻し
//   3 秒   : 周り 9〜12m に雑魚 20 体が寄ってくる → 7.5 秒 look tracks（足跡）
//   8 秒   : 全消し、正面 7m で爆発の範囲（カメラが揺れる印 = クレーター）→ 8.4 秒 look crater、9 秒 凹みの深さ
//   9.5 秒 : 正面 7m で凍らせる範囲（氷の魔法と同じ印 = 濡らして冷やす）→ 10.2 秒 look ice、乾きの値
//   11 秒  : 竜巻くらいの風源が正面 7m を左から右へ 3 秒（2026-11-03 風に舞う砂）→ look blow1 / blow2、
//            風源の所と 14m 先の舞う強さ、クレーターがどれだけ埋まったか
//   14.5 秒: 一番大きい池を見る look oasis（池の周りの草）
//   16 秒  : プレイヤーが前へ 1.6 秒走る（途中を濡らす）→ look footprints（近く）/ footprints14（普段のカメラ）、
//            19 秒から足跡の上に風源 → 2 秒で何枚残るか。21.2 秒 done（2026-11-03 足跡）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "World/SandField.h"

namespace
{
    class AutoTestSand final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Probe(const char* tag, const Vector3& p);
        void Look(const Vector3& p);
        Vector3 Front(float side, float ahead) const { return Vector3(m_Base.x + side, m_Base.y, m_Base.z + ahead); }
        Vector3 m_Base;
        Vector3 m_LookAt;
        Vector3 m_Pond;
        bool m_HasPond = false;
        Vector3 m_WalkFrom;
        int m_KickShot = 0;
    };
}

void AutoTestSand::Probe(const char* tag, const Vector3& p)
{
    float temp = 0.0f, dry = 0.0f, depth = 0.0f, lift = 0.0f;
    const bool ok = m_Sand.DebugReadAt(p, temp, dry, depth, &lift);
    char line[200];
    snprintf(line, sizeof(line), "sand probe %s ok %d at (%.1f, %.1f) temp %.3f dry %.3f depth %.3f lift %.3f",
        tag, ok ? 1 : 0, p.x, p.z, temp, dry, depth, lift);
    AutoTestLog(line);
}

void AutoTestSand::Look(const Vector3& p)
{
    m_LookAt = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 0.9f, p.z);
    m_Camera.SetTargetOverride(&m_LookAt);
    m_Camera.Camera().SnapToTarget();
}

void AutoTestSand::Run()
{
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_GameUI.hideAll = true;   // HUD の技のアイコンが足元の砂を隠す
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_Base = m_Registry.Get<TransformComponent>(m_Player).position;

        int grass = 0;
        for (uint8_t g : m_GrassMask) grass += g ? 1 : 0;
        int ponds = 0;
        for (uint8_t p : m_TerrainMap.pond) ponds += p ? 1 : 0;
        char line[220];
        snprintf(line, sizeof(line), "sand start stage %d active %d grassCells %d pondCells %d lod parts %u tris %u",
            m_StageIndex, m_Sand.Active() ? 1 : 0, grass, ponds, m_Swarm.LodPartDraws(), m_Swarm.LodMergedIndexCount() / 3);
        AutoTestLog(line);

        // 一番大きい池（周り 5×5 の水のマスが多い所）
        int best = -1;
        const float kCs = GridWorld::kCellSize;
        for (int gz = 2; gz < m_Grid.Depth() - 2; ++gz)
            for (int gx = 2; gx < m_Grid.Width() - 2; ++gx)
            {
                if (!m_Grid.IsWater(gx, gz)) continue;
                int around = 0;
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dx = -2; dx <= 2; ++dx)
                        around += m_Grid.IsWater(gx + dx, gz + dz) ? 1 : 0;
                if (around <= best) continue;
                best = around;
                m_Pond = Vector3(m_Grid.OriginX() + (gx + 0.5f) * kCs, 0.0f, m_Grid.OriginZ() + (gz + 0.5f) * kCs);
            }
        m_HasPond = best >= 0;
        Look(Front(0.0f, 7.0f));
        m_AutoStep = 1;
    }
    if (m_AutoStep == 1 && m_AutoTime >= 1.5f)
    {
        m_Sand.Stamp(Front(-3.5f, 7.0f), 2.5f, 1.2f, 0.0f);
        m_AutoStep = 2;
    }
    if (m_AutoStep == 2 && m_AutoTime >= 1.55f)
    {
        m_Sand.Stamp(Front(3.5f, 7.0f), 2.5f, 0.0f, 1.0f);   // 1 歩に 1 つなので次のフレーム
        m_AutoStep = 3;
    }
    if (m_AutoStep == 3 && m_AutoTime >= 2.3f)
    {
        AutoTestLog("sand look heatwet");
        Probe("hot", Front(-3.5f, 7.0f));
        Probe("wet", Front(3.5f, 7.0f));
        Probe("plain", Front(0.0f, 14.0f));
        m_AutoStep = 4;
    }
    if (m_AutoStep == 4 && m_AutoTime >= 3.0f)
    {
        const float gy = m_Swarm.GetAIParams().groundY;
        for (int k = 0; k < 20; ++k)
        {
            const float a = 2.39996f * (float)k, r = 9.0f + 3.0f * std::fmod(0.618f * (float)k, 1.0f);
            const float x = m_Base.x + std::sin(a) * r, z = m_Base.z + std::cos(a) * r;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 100000.0f, 3.0f, Swarm::kEnemyKindMob);
        }
        Look(m_Base);
        m_AutoStep = 5;
    }
    if (m_AutoStep == 5 && m_AutoTime >= 7.5f)
    {
        AutoTestLog("sand look tracks");
        Probe("dry later", Front(3.5f, 7.0f));
        Probe("near player", Front(0.0f, 1.5f));
        m_AutoStep = 6;
    }
    if (m_AutoStep == 6 && m_AutoTime >= 8.0f)
    {
        m_Swarm.KillAll();
        Swarm::Area a;
        a.center = Front(0.0f, 7.0f);
        a.center.y = m_Grid.SampleHeight(a.center.x, a.center.z);
        a.radius = 3.0f;
        a.halfHeight = 4.0f;
        a.damage = 0.0f;
        a.tickInterval = 1.0f;
        a.timeLeft = 0.3f;
        a.flags = Swarm::kAreaShake;
        m_Swarm.SpawnArea(a);
        Look(Front(0.0f, 7.0f));
        m_AutoStep = 7;
    }
    if (m_AutoStep == 7 && m_AutoTime >= 8.4f) { AutoTestLog("sand look crater"); m_AutoStep = 8; }
    if (m_AutoStep == 8 && m_AutoTime >= 9.0f) { Probe("crater", Front(0.0f, 7.0f)); m_AutoStep = 9; }
    if (m_AutoStep == 9 && m_AutoTime >= 9.5f)
    {
        Swarm::Area a;
        a.center = Front(-4.0f, 4.0f);
        a.center.y = m_Grid.SampleHeight(a.center.x, a.center.z);
        a.radius = 2.5f;
        a.halfHeight = 4.0f;
        a.damage = 0.0f;
        a.tickInterval = 1.0f;
        a.timeLeft = 0.3f;
        a.flags = 6u << Swarm::kAreaFreezeShift;
        m_Swarm.SpawnArea(a);
        Look(Front(-4.0f, 4.0f));
        m_AutoStep = 10;
    }
    if (m_AutoStep == 10 && m_AutoTime >= 10.2f)
    {
        AutoTestLog("sand look ice");
        Probe("ice", Front(-4.0f, 4.0f));
        m_AutoStep = 11;
    }
    // 竜巻くらいの風源が正面 7m を左から右へ 4 m/s で通る（クレーターの上も通る）→ 砂が舞う、凹みが早く埋まる
    if (((m_AutoStep >= 11 && m_AutoStep <= 14) || m_AutoStep == 19) && m_AutoTime >= 11.0f)
    {
        const float t = m_AutoTime - 11.0f;
        Vector3 p = Front(-6.0f + 4.0f * t, 7.0f);
        p.y = m_Grid.SampleHeight(p.x, p.z);
        Swarm::WindSource w = {};
        w.pos[0] = p.x; w.pos[1] = p.y; w.pos[2] = p.z;
        w.radius = 5.5f;
        w.swirl = 13.0f;
        w.updraft = 4.5f;
        w.inflow = 1.8f;
        w.fade = 1.0f;
        if (t < 3.0f) m_Swarm.AddFrameWindSource(w);
        if (m_AutoStep == 11) { Look(Front(0.0f, 7.0f)); m_AutoStep = 12; }
        if (m_AutoStep == 12 && t >= 1.2f)
        {
            AutoTestLog("sand look blow1");
            float temp, dry, depth, lift;
            m_Sand.DebugReadAt(p + Vector3(0.0f, 0.0f, 2.0f), temp, dry, depth, &lift);   // 渦の真ん中は横の風が無い（r 0.3 辺りが一番強い）
            char line[200];
            snprintf(line, sizeof(line), "sand wind 2m from the core lift %.3f dry %.3f", lift, dry);
            AutoTestLog(line);
            m_Sand.DebugReadAt(Front(0.0f, 14.0f), temp, dry, depth, &lift);
            snprintf(line, sizeof(line), "sand wind far away lift %.3f", lift);
            AutoTestLog(line);
            m_AutoStep = 13;
        }
        // 近くから（14m だと砂粒が小さくて見えない）。風源の 2m 手前を低い角度で
        if (m_AutoStep == 13 && t >= 1.8f)
        {
            auto& cam = m_Camera.Camera();
            m_LookAt = Vector3(p.x, p.y + 0.6f, p.z);
            m_Camera.SetTargetOverride(&m_LookAt);
            cam.distance = 6.0f;
            cam.SetPitch(16.0f);
            cam.SnapToTarget();
            m_AutoStep = 14;
        }
        if (m_AutoStep == 14 && t >= 2.2f) { AutoTestLog("sand look blow2"); m_AutoStep = 19; }
        if (m_AutoStep == 19 && t >= 3.3f)
        {
            auto& cam = m_Camera.Camera();
            cam.distance = 14.0f;
            cam.SetPitch(45.0f);
            Probe("crater after wind", Front(0.0f, 7.0f));
            m_AutoStep = 15;
        }
    }
    if (m_AutoStep == 15 && m_AutoTime >= 14.5f)
    {
        if (m_HasPond)
        {
            Look(m_Pond);
            char line[120];
            snprintf(line, sizeof(line), "sand pond at (%.1f, %.1f)", m_Pond.x, m_Pond.z);
            AutoTestLog(line);
        }
        m_AutoStep = 16;
    }
    if (m_AutoStep == 16 && m_AutoTime >= 15.3f) { AutoTestLog("sand look oasis"); m_AutoStep = 17; }
    // ---- プレイヤーの足跡（2026-11-03）：濡らした所を通って前へ走り、止まって後ろを見る → 風を当てて消えるか ----
    if (m_AutoStep == 17 && m_AutoTime >= 16.0f)
    {
        m_Camera.SetTargetOverride(nullptr);
        m_Footprints.Clear();
        const Vector3 p0 = m_Registry.Get<TransformComponent>(m_Player).position;
        m_WalkFrom = p0;
        m_Sand.Stamp(Vector3(p0.x, p0.y, p0.z + 4.0f), 1.6f, 0.0f, 1.0f);   // 通り道の途中を濡らす（濃い足跡）
        m_PlayerControlSystem.testInput = true;
        {
            auto& cam = m_Camera.Camera();   // 走る後ろから近めに（蹴り上げる砂粒を見る）
            cam.distance = 7.0f;
            cam.SetPitch(28.0f);
            cam.SnapToTarget();
        }
        m_PlayerControlSystem.testMove = Vector2(0.0f, 1.0f);
        m_AutoStep = 20;
    }
    // 走っている間に 4 枚（一歩の砂粒が写る瞬間を拾う）
    if (m_AutoStep == 20 && m_KickShot < 4 && m_AutoTime >= 16.6f + 0.13f * (float)m_KickShot)
    {
        char line[64];
        snprintf(line, sizeof(line), "sand look kick%d", m_KickShot);
        AutoTestLog(line);
        ++m_KickShot;
    }
    if (m_AutoStep == 20 && m_AutoTime >= 17.6f)
    {
        m_PlayerControlSystem.testMove = Vector2::Zero;
        const Vector3 p1 = m_Registry.Get<TransformComponent>(m_Player).position;
        char line[200];
        snprintf(line, sizeof(line), "sand walk from (%.1f, %.1f) to (%.1f, %.1f) prints %d cpuVfx %d",
            m_WalkFrom.x, m_WalkFrom.z, p1.x, p1.z, m_Footprints.Count(), (int)m_AreaVFX.GetActiveCount());
        AutoTestLog(line);
        Look((m_WalkFrom + p1) * 0.5f);
        auto& cam = m_Camera.Camera();
        cam.distance = 6.5f;
        cam.SetPitch(42.0f);
        cam.SnapToTarget();
        m_AutoStep = 21;
    }
    if (m_AutoStep == 21 && m_AutoTime >= 18.2f) { AutoTestLog("sand look footprints"); m_AutoStep = 22; }
    if (m_AutoStep == 22 && m_AutoTime >= 18.3f)
    {
        auto& cam = m_Camera.Camera();   // ごく近く（凹みの明暗・縁の盛り上がり・砂粒）
        cam.distance = 3.0f;
        cam.SetPitch(36.0f);
        cam.SnapToTarget();
        m_AutoStep = 27;
    }
    if (m_AutoStep == 27 && m_AutoTime >= 19.0f) { AutoTestLog("sand look footprintsclose"); m_AutoStep = 28; }
    if (m_AutoStep == 28 && m_AutoTime >= 19.1f)
    {
        auto& cam = m_Camera.Camera();
        cam.distance = 14.0f;
        cam.SetPitch(45.0f);
        cam.SnapToTarget();
        m_AutoStep = 23;
    }
    if (m_AutoStep == 23 && m_AutoTime >= 19.6f) { AutoTestLog("sand look footprints14"); m_AutoStep = 24; }
    if (m_AutoStep >= 24 && m_AutoStep <= 25 && m_AutoTime >= 19.7f)
    {
        // 足跡の上に風源（竜巻くらい）を置き続ける
        const Vector3 p1 = m_Registry.Get<TransformComponent>(m_Player).position;
        const Vector3 c = (m_WalkFrom + p1) * 0.5f;
        Swarm::WindSource w = {};
        w.pos[0] = c.x + 2.0f; w.pos[1] = c.y; w.pos[2] = c.z;
        w.radius = 6.0f; w.swirl = 13.0f; w.updraft = 4.5f; w.inflow = 1.8f; w.fade = 1.0f;
        m_Swarm.AddFrameWindSource(w);
        if (m_AutoStep == 24)
        {
            char line[120];
            snprintf(line, sizeof(line), "sand wind on prints before %d", m_Footprints.Count());
            AutoTestLog(line);
            m_AutoStep = 25;
        }
        if (m_AutoStep == 25 && m_AutoTime >= 21.7f)
        {
            char line[120];
            snprintf(line, sizeof(line), "sand wind on prints after 2s %d", m_Footprints.Count());
            AutoTestLog(line);
            m_AutoStep = 26;
        }
    }
    if (m_AutoStep == 26 && m_AutoTime >= 21.9f)
    {
        m_PlayerControlSystem.testInput = false;
        m_Camera.SetTargetOverride(nullptr);
        m_GameUI.hideAll = false;
        AutoTestLog("sand done");
        m_AutoStep = 18;
    }
}

REGISTER_BATTLE_AUTOTEST("sand", AutoTestSand)
