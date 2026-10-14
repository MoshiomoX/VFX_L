// ============================================================
// AutoTestKnock.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=knock
// VFXL_BATTLE_AUTOTEST=knock：止まったプレイヤーを雑魚 1 体に殴らせ、次に自爆兵 1 体を爆発させて、ノックバックで動いた距離と高さを記録
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestKnock final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 被弾のノックバック（VFXL_BATTLE_AUTOTEST=knock、2026-10-01）
// 1 秒: 湧き停止・全消し・無敵・詠唱停止、入力は 0（testInput）。プレイヤーの前 3m に雑魚 1 体（殴られる）。
// 6 秒: 全消しして前 2m に自爆兵 1 体（触れて点火 → 爆発）。
// ノックバックが入る度（knockTime が増えた時）に位置を覚え、knockDuration + 0.15 秒後に
// `knock <melee|blast> dist <水平に動いた m> lift <最高点 - 開始の高さ> dir (x,z)` を記録。12 秒 done
// ============================================================
void AutoTestKnock::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<WandComponent>(m_Player))
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;

    static Vector3 s_Start;
    static float s_Watch = -1.0f, s_PeakY = 0.0f, s_LastKnock = 0.0f;
    static bool s_Blast = false;
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
    const auto& stats = m_Registry.Get<PlayerStatsComponent>(m_Player);
    const float gy = m_Swarm.GetAIParams().groundY;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Swarm.SpawnEnemy(Vector3(tf.position.x, gy, tf.position.z + 3.0f), 100000.0f, 3.5f);
        char line[128];
        snprintf(line, sizeof(line), "knock start body %.2f m  melee %.2f bodies  blast %.2f bodies lift %.1f",
            stats.radius * 2.0f, stats.knockMeleeBodies, stats.knockBlastBodies, stats.knockBlastLift);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 6.0f)
    {
        m_Swarm.KillAll();
        m_Swarm.SpawnEnemy(Vector3(tf.position.x, gy, tf.position.z + 2.0f), 100000.0f, 3.5f, Swarm::kEnemyKindBomber);
        AutoTestLog("knock bomber");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 12.0f)
    {
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("knock done");
        m_AutoStep = 3;
    }

    // ノックバックが入った（残り時間が増えた）→ 測り始める
    if (st.knockTime > s_LastKnock + 1e-4f && st.knockTime >= st.knockDuration - 1e-4f)
    {
        s_Start = tf.position;
        s_PeakY = tf.position.y;
        s_Blast = std::fabs(st.knockDuration - stats.knockBlastTime) < 1e-4f;
        s_Watch = m_AutoTime + st.knockDuration + 0.15f;
    }
    s_LastKnock = st.knockTime;
    if (s_Watch > 0.0f)
    {
        s_PeakY = (std::max)(s_PeakY, tf.position.y);
        if (m_AutoTime >= s_Watch)
        {
            const Vector3 d = tf.position - s_Start;
            char line[160];
            snprintf(line, sizeof(line), "knock %s dist %.3f lift %.3f dir (%.2f,%.2f) hp %.0f",
                s_Blast ? "blast" : "melee", std::sqrt(d.x * d.x + d.z * d.z), s_PeakY - s_Start.y,
                st.knockX, st.knockZ, m_Registry.Get<HealthComponent>(m_Player).current);
            AutoTestLog(line);
            s_Watch = -1.0f;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("knock", AutoTestKnock)
