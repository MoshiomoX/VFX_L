// ============================================================
// AutoTestBossExit.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=bossexit
// VFXL_BATTLE_AUTOTEST=bossexit：洞窟の奥で Boss を呼び、プレイヤーは洞の外の平原へ。Boss が口から出て来られるか
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBossExit final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: Boss が洞窟から出られるか（VFXL_BATTLE_AUTOTEST=bossexit、2026-10-03）
// 1 秒: 湧き停止・全消し・無敵・詠唱停止、門の前 2m へ。1.5 秒に F（Boss を呼ぶ）。
// 3 秒: プレイヤーを洞窟の 1 本目の坂の上端から外へ 12m（平原）へ、カメラは口の方を向く。
// 毎秒 `bossexit t alive pos y inCave dist`、Boss が口から 6m 以内に来たら `bossexit look mouth`（1 回）、
// 45 秒 `bossexit look end` と done
// ============================================================
void AutoTestBossExit::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    char line[200];
    static float s_NextLog = 0.0f;
    static bool s_ShotMouth = false;
    const auto& lay = m_TerrainLayout;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        const float yaw = DirectX::XMConvertToRadians(m_Stage.GetPortalYaw());
        const Vector3 p = m_Stage.GetPortalCenter() + Vector3(std::sin(yaw), 0.0f, std::cos(yaw)) * 2.0f;
        tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
        m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 1.5f) { m_AutoInteract = true; m_AutoStep = 2; }
    else if (m_AutoStep == 2 && m_AutoTime >= 3.0f)
    {
        if (lay.mineRamps.empty()) { AutoTestLog("bossexit no mine ramp"); m_AutoStep = 9; return; }
        const auto& r = lay.mineRamps[0];
        const Vector3 p = r.top - r.down * 12.0f;
        tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
        m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-r.down.x, r.down.z)));   // 口の方を見る
        cam.distance = 10.0f;
        cam.SetPitch(18.0f);
        cam.SnapToTarget();
        s_NextLog = m_AutoTime;
        s_ShotMouth = false;
        snprintf(line, sizeof(line), "bossexit player outside at %.0f,%.1f,%.0f, mouth at %.0f,%.0f",
            p.x, tf.position.y, p.z, r.top.x, r.top.z);
        AutoTestLog(line);
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3)
    {
        if (m_AutoTime >= s_NextLog)
        {
            s_NextLog += 1.0f;
            const auto& bi = m_Swarm.GetBossInfo();
            const Vector3 bp(bi.pos[0], bi.pos[1], bi.pos[2]);
            const float ground = m_Grid.SampleHeight(bp.x, bp.z);
            const Vector3 mouth = lay.mineRamps[0].top;
            const float toMouth = (Vector3(bp.x, 0, bp.z) - Vector3(mouth.x, 0, mouth.z)).Length();
            snprintf(line, sizeof(line), "bossexit t %.0f alive %u pos %.1f,%.1f,%.1f ground %.1f inCave %d toMouth %.1f toPlayer %.1f",
                m_AutoTime - 3.0f, bi.alive, bp.x, bp.y, bp.z, ground, ground < -5.0f ? 1 : 0, toMouth,
                (Vector3(bp.x, 0, bp.z) - Vector3(tf.position.x, 0, tf.position.z)).Length());
            AutoTestLog(line);
            if (!s_ShotMouth && bi.alive && toMouth < 6.0f)
            {
                AutoTestLog("bossexit look mouth");
                s_ShotMouth = true;
            }
        }
        if (m_AutoTime >= 48.0f) { AutoTestLog("bossexit look end"); m_AutoStep = 4; }
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 48.8f) { AutoTestLog("bossexit done"); m_AutoStep = 5; }
}

REGISTER_BATTLE_AUTOTEST("bossexit", AutoTestBossExit)
