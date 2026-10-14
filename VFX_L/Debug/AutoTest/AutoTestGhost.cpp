// ============================================================
// AutoTestGhost.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=ghost
// VFXL_BATTLE_AUTOTEST=ghost：最終ウェーブの幽霊を 20 体出して、数・壁抜け・近づく速さを記録し撮る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestGhost final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 最終ウェーブの幽霊（VFXL_BATTLE_AUTOTEST=ghost）
// 1 秒: 湧き停止・全消し・無敵、幽霊を 20 体（プレイヤーの周りの環 25〜35m。壁を素通りするので歩けるマスかは見ない）。
// 0.5 秒毎に敵の池を読み戻し、生きている数・塞がったマスに居る数（幽霊は壁の中を通れる = 0 でなくてよい）・
// プレイヤーに一番近い幽霊の距離を記録。3 秒 / 5 秒 "ghost look"（外から撮る）、8 秒 done
// ============================================================
void AutoTestGhost::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    static float s_NextLog = 0.0f;
    static int s_Looks = 0;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Mobs.QueueDebugGhosts(20);
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;   // 撃たない（数と動きを見る）
        AutoTestLog("ghost start: 20 ghosts");
        s_NextLog = m_AutoTime + 0.5f;
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1)
    {
        if (m_AutoTime >= s_NextLog)
        {
            s_NextLog = m_AutoTime + 0.5f;
            std::vector<Swarm::Enemy> enemies;
            std::vector<uint32_t> states;
            int alive = 0, inWall = 0;
            float nearest = 1e9f;
            const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
            if (m_Swarm.DebugReadEnemies(enemies, states))
                for (size_t i = 0; i < enemies.size(); ++i)
                {
                    if (states[i] == Swarm::kStateDead) continue;
                    ++alive;
                    int gx, gz;
                    m_Grid.WorldToCell(enemies[i].position, gx, gz);
                    if (!m_Grid.IsWalkable(gx, gz)) ++inWall;
                    const Vector3 d = enemies[i].position - pp;
                    nearest = (std::min)(nearest, std::sqrt(d.x * d.x + d.z * d.z));
                }
            char line[128];
            snprintf(line, sizeof(line), "ghost t %.1f alive %d inWall %d nearest %.1f", m_AutoTime, alive, inWall, nearest);
            AutoTestLog(line);
        }
        if ((s_Looks == 0 && m_AutoTime >= 3.0f) || (s_Looks == 1 && m_AutoTime >= 5.0f))
        {
            char line[32];
            snprintf(line, sizeof(line), "ghost look %d", s_Looks++);
            AutoTestLog(line);
        }
        if (m_AutoTime >= 8.0f) { AutoTestLog("ghost done"); m_AutoStep = 2; }
    }
}

REGISTER_BATTLE_AUTOTEST("ghost", AutoTestGhost)
