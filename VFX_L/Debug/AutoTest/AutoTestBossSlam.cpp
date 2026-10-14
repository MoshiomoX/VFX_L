// ============================================================
// AutoTestBossSlam.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=bossslam
// VFXL_BATTLE_AUTOTEST=bossslam：Boss を呼び、立ち止まる（当たる）/ 円を走る（外れる）でスラムの輪を試す
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBossSlam final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: Boss のスラムの輪（VFXL_BATTLE_AUTOTEST=bossslam、2026-10-03）
// 1 秒: 湧き停止・全消し・詠唱停止、プレイヤーの HP 10 万（無敵にすると当たりを数えられない）。
// Boss は動かない（bossSpeed 0）・HP 大。門の前で F → 2.5 秒でプレイヤーを Boss から 12〜16m の同じ高さの歩けるマスへ、
// カメラはプレイヤーの背後から Boss の方を 13m / 38° で見る。最初の技は 2.5 秒後（firstDelay。プレイヤーを置き直した後）。
// A 段（〜13 秒）は立ち止まる（当たるはず）、B 段（〜24 秒）は半径 5m の円を走る（外れるはず）。
// 0.5 秒毎に `bossslam t phase alive rings volleys placed blasts hits hp`。各段で輪が 0.6 まで育った所で
// `bossslam look rings<A|B>`、爆発の 0.1 秒後に `bossslam look blast<A|B>`、`bossslam done`
// ============================================================
void AutoTestBossSlam::Run(float dt)
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    auto& hp = m_Registry.Get<HealthComponent>(m_Player);
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& cam = m_Camera.Camera();
    auto& pcs = m_PlayerControlSystem;
    char line[300];
    static float s_NextLog = 0.0f, s_PhaseStart = 0.0f, s_Angle = 0.0f;
    static Vector3 s_Spot;
    static bool s_ShotRings[2] = {}, s_ShotBlast[2] = {};
    static uint32_t s_LastBlasts = 0;
    static float s_BlastShotAt = -1.0f;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        hp.invincible = false;
        hp.max = hp.current = 100000.0f;
        m_Stage.bossSpeed = 0.0f;
        m_Stage.bossHp = 1.0e6f;
        m_BossAttacks.firstDelay = 2.5f;   // 最初の技はプレイヤーを置き直した後（3.5 秒）
        for (int k = 0; k < (int)BossMove::Count; ++k)   // 2026-10-07 から技が 4 つ：この自動テストは重撃だけを見る
            m_BossAttacks.brain.rules[k].enabled = (k == (int)BossMove::Slam);
        const float yaw = DirectX::XMConvertToRadians(m_Stage.GetPortalYaw());
        const Vector3 p = m_Stage.GetPortalCenter() + Vector3(std::sin(yaw), 0.0f, std::cos(yaw)) * 2.0f;
        tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
        m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 1.5f) { m_AutoInteract = true; m_AutoStep = 2; }
    else if (m_AutoStep == 2 && m_AutoTime >= 3.5f)
    {
        if (!m_Stage.IsBossAlive()) { AutoTestLog("bossslam no boss"); AutoTestLog("bossslam done"); m_AutoStep = 9; return; }
        // Boss から 12〜16m、Boss の足元と同じ高さで 3x3 が歩けるマス
        const Vector3 bp = m_Stage.BossPos();
        const float by = m_Grid.SampleHeight(bp.x, bp.z);
        bool found = false;
        for (float r = 14.0f; r >= 9.0f && !found; r -= 1.0f)
            for (int a = 0; a < 24 && !found; ++a)
            {
                const float ang = 6.2831853f * (float)a / 24.0f;
                const Vector3 c = bp + Vector3(std::cos(ang), 0.0f, std::sin(ang)) * r;
                int gx = 0, gz = 0;
                m_Grid.WorldToCell(c, gx, gz);
                bool ok = true;
                for (int dz = -1; dz <= 1 && ok; ++dz)
                    for (int dx = -1; dx <= 1 && ok; ++dx)
                        ok = m_Grid.IsWalkable(gx + dx, gz + dz);
                if (!ok || std::fabs(m_Grid.SampleHeight(c.x, c.z) - by) > 0.3f) continue;
                s_Spot = Vector3(c.x, m_Grid.SampleHeight(c.x, c.z), c.z);
                found = true;
            }
        if (!found) s_Spot = Vector3(bp.x + 10.0f, by, bp.z);
        tf.position = s_Spot + Vector3(0.0f, 1.0f, 0.0f);
        m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        const Vector3 toBoss = bp - s_Spot;
        cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-toBoss.x, toBoss.z)));   // forward.x = -sin(yaw)
        cam.distance = 13.0f;
        cam.SetPitch(38.0f);
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "bossslam player at %.1f,%.1f,%.1f boss at %.1f,%.1f,%.1f dist %.1f found %d",
            s_Spot.x, s_Spot.y, s_Spot.z, bp.x, bp.y, bp.z, Vector3(toBoss.x, 0.0f, toBoss.z).Length(), found ? 1 : 0);
        AutoTestLog(line);
        s_NextLog = m_AutoTime;
        s_PhaseStart = m_AutoTime;
        s_ShotRings[0] = s_ShotRings[1] = s_ShotBlast[0] = s_ShotBlast[1] = false;
        s_LastBlasts = m_BossAttacks.blasts;
        s_BlastShotAt = -1.0f;
        m_BossSlamHits = 0;
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3)
    {
        const float t = m_AutoTime - s_PhaseStart;
        const int phase = (t < 9.5f) ? 0 : 1;   // A: 立ち止まる / B: 円を走る
        const char* phaseName = phase == 0 ? "A" : "B";
        Vector3 camF = cam.GetForward(); camF.y = 0.0f; camF.Normalize();
        Vector3 camR = cam.GetRight();   camR.y = 0.0f; camR.Normalize();
        pcs.testInput = true;
        pcs.testSlide = false;
        pcs.testJump = false;
        if (phase == 0) pcs.testMove = Vector2::Zero;
        else
        {
            s_Angle += dt * 1.0f;
            const Vector3 goal = s_Spot + Vector3(std::cos(s_Angle + 0.6f), 0.0f, std::sin(s_Angle + 0.6f)) * 5.0f;
            Vector3 d = goal - tf.position;
            d.y = 0.0f;
            if (d.LengthSquared() > 1e-4f) d.Normalize();
            pcs.testMove = Vector2(d.Dot(camR), d.Dot(camF));
        }

        // 撮影: 輪が 0.6 まで育った所 / 爆発の直後（各段 1 回）
        if (!s_ShotRings[phase])
            for (const BossAttacks::Visual& v : m_BossAttacks.Visuals())
                if (v.progress >= 0.6f)
                {
                    snprintf(line, sizeof(line), "bossslam look rings%s", phaseName);
                    AutoTestLog(line);
                    s_ShotRings[phase] = true;
                    break;
                }
        if (m_BossAttacks.blasts != s_LastBlasts)
        {
            s_LastBlasts = m_BossAttacks.blasts;
            if (!s_ShotBlast[phase] && s_BlastShotAt < 0.0f) s_BlastShotAt = m_AutoTime + 0.1f;
        }
        if (s_BlastShotAt > 0.0f && m_AutoTime >= s_BlastShotAt)
        {
            snprintf(line, sizeof(line), "bossslam look blast%s", phaseName);
            AutoTestLog(line);
            s_ShotBlast[phase] = true;
            s_BlastShotAt = -1.0f;
        }

        if (m_AutoTime >= s_NextLog)
        {
            s_NextLog += 0.5f;
            snprintf(line, sizeof(line), "bossslam t %.1f phase %s alive %d rings %d volleys %u placed %u blasts %u hits %u hp %.0f",
                t, phaseName, m_Stage.IsBossAlive() ? 1 : 0, (int)m_BossAttacks.Visuals().size(), m_BossAttacks.volleys,
                m_BossAttacks.ringsPlaced, m_BossAttacks.blasts, m_BossSlamHits, hp.current);
            AutoTestLog(line);
        }
        if (t >= 9.5f && t - dt < 9.5f)
        {
            snprintf(line, sizeof(line), "bossslam phase A end hits %u blasts %u", m_BossSlamHits, m_BossAttacks.blasts);
            AutoTestLog(line);
        }
        if (t >= 21.0f)
        {
            pcs.testInput = false;
            snprintf(line, sizeof(line), "bossslam total hits %u blasts %u volleys %u", m_BossSlamHits, m_BossAttacks.blasts,
                m_BossAttacks.volleys);
            AutoTestLog(line);
            AutoTestLog("bossslam done");
            m_AutoStep = 9;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("bossslam", AutoTestBossSlam)
