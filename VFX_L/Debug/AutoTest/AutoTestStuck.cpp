// ============================================================
// AutoTestStuck.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=stuck
// VFXL_BATTLE_AUTOTEST=stuck：雑魚をわざと塞がったマスの中に出し、壁から出てくるか・二度と入らないかを敵の池の読み戻しで数える
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestStuck final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 雑魚の壁抜け・壁詰まり（VFXL_BATTLE_AUTOTEST=stuck）
// 1 秒: 湧き停止・全消し・無敵。プレイヤーの周り 6〜24m の「塞がったマス」（木・岩・台地の箱。外周は除く）を
//       最大 8 個選び、その中心に雑魚を 3 体ずつ「わざと壁の中に」出す。普通の位置にも 40 体
// 以後 0.5 秒毎に敵の池を読み戻し、生きている雑魚のうち塞がったマスに居る数を記録（`stuck t alive inWall`）。
// 期待: 数秒で inWall が 0 になり、その後も 0 のまま（壁から出て、二度と入らない）。12 秒 done
// ============================================================
void AutoTestStuck::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    static float s_NextLog = 0.0f;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;

        // 塞がったマス（外周の 1 マスは除く）を近い順に
        std::vector<std::pair<float, Vector3>> blocked;
        for (int gz = 1; gz < m_Grid.Depth() - 1; ++gz)
            for (int gx = 1; gx < m_Grid.Width() - 1; ++gx)
            {
                if (m_Grid.IsWalkable(gx, gz)) continue;
                const Vector3 c = m_Grid.CellToWorld(gx, gz);
                const float d = (Vector3(c.x, 0, c.z) - Vector3(pp.x, 0, pp.z)).Length();
                if (d < 6.0f || d > 24.0f) continue;
                blocked.push_back({ d, c });
            }
        std::sort(blocked.begin(), blocked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        int cells = 0;
        for (size_t i = 0; i < blocked.size() && cells < 8; ++i, ++cells)
            for (int k = 0; k < 3; ++k)
                m_Swarm.SpawnEnemy(Vector3(blocked[i].second.x + (k - 1) * 0.3f, gy, blocked[i].second.z), 100000.0f, 3.5f);
        for (int k = 0; k < 40; ++k)
        {
            const float a = (float)k * 0.157f;
            const float r = 8.0f + (float)(k % 5) * 2.0f;
            const Vector3 p(pp.x + std::cos(a) * r, gy, pp.z + std::sin(a) * r);
            int gx, gz;
            m_Grid.WorldToCell(p, gx, gz);
            if (m_Grid.IsWalkable(gx, gz)) m_Swarm.SpawnEnemy(p, 100000.0f, 3.5f);
        }
        char line[96];
        snprintf(line, sizeof(line), "stuck start: %d blocked cells x3 inside walls, 40 normal", cells);
        AutoTestLog(line);
        s_NextLog = m_AutoTime + 0.05f;   // 最初の 1 回は出した直後（壁の中に居ることを確認）
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.5f;
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        int alive = 0, inWall = 0, nan = 0;
        if (m_Swarm.DebugReadEnemies(enemies, states))
        {
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                ++alive;
                const Vector3 p = enemies[i].position;
                if (p.x != p.x || p.z != p.z) { ++nan; continue; }
                int gx, gz;
                m_Grid.WorldToCell(p, gx, gz);
                if (!m_Grid.IsWalkable(gx, gz)) ++inWall;
            }
        }
        char line[96];
        snprintf(line, sizeof(line), "stuck t %.1f alive %d inWall %d nan %d", m_AutoTime, alive, inWall, nan);
        AutoTestLog(line);
        if (m_AutoTime >= 12.0f) { AutoTestLog("stuck done"); m_AutoStep = 2; }
    }
}

REGISTER_BATTLE_AUTOTEST("stuck", AutoTestStuck)
