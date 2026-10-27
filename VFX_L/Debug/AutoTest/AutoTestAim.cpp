// ============================================================
// AutoTestAim.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=aim
// 杖の狙いの優先度（2026-10-26 ユーザー「範囲に Boss が居れば Boss を先に、雑魚が近過ぎれば雑魚を先に」。
// SwarmCommon の SwarmAimKeyDist、BomberCB::aimCloseRadius / aimPriorityRange）：
//   1 秒: 湧き停止・全消し・無敵・詠唱停止。正面 20m に動かない Boss（HP 100 万）、周り 8〜12m に動かない雑魚 8 体
//   2.5 秒: `aim A nearest <距離> kind <種類>` → Boss（kind 3）のはず（雑魚は 8m 以上離れている）
//   3 秒: 正面 2.5m に雑魚 1 体 → 4.5 秒: `aim B …` → 雑魚（kind 0）のはず（aimCloseRadius 3.5m の中）
//   5 秒: その雑魚を消す代わりに aimPriorityOn = 0 → 6.5 秒: `aim C …` → 純粋な最寄り = 2.5m の雑魚、
//   7 秒: aimPriorityOn = 1、近い雑魚を 6m へ（KillAll して置き直し）→ 8.5 秒: `aim D …` → Boss。9 秒: done
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestAim final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Place(bool closeMob, float closeDist);
        void Log(const char* tag);
    };
}

void AutoTestAim::Place(bool closeMob, float closeDist)
{
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const float groundY = m_Swarm.GetAIParams().groundY;
    auto at = [&](float dx, float dz) { const float x = tf.position.x + dx, z = tf.position.z + dz; return Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z); };
    m_Swarm.KillAll();
    m_Swarm.SpawnEnemy(at(0.0f, 20.0f), 1000000.0f, 0.0f, Swarm::kEnemyKindBoss);
    for (int k = 0; k < 8; ++k)
    {
        const float ang = 6.2831853f * (float)k / 8.0f, r = 8.0f + 0.5f * (float)k;
        m_Swarm.SpawnEnemy(at(std::sin(ang) * r, std::cos(ang) * r), 100000.0f, 0.0f, Swarm::kEnemyKindMob);
    }
    if (closeMob) m_Swarm.SpawnEnemy(at(0.0f, closeDist), 100000.0f, 0.0f, Swarm::kEnemyKindMob);
}

void AutoTestAim::Log(const char* tag)
{
    char line[200];
    Vector3 np, nv; float nd = 0.0f;
    const bool has = m_Swarm.GetNearestEnemy(np, nv, nd);
    int kind = -1;
    std::vector<Swarm::Enemy> en; std::vector<uint32_t> st; std::vector<Swarm::EnemyExtra> ex;
    if (has && m_Swarm.DebugReadEnemies(en, st, &ex))
    {
        for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
        {
            if (st[i] == Swarm::kStateDead) continue;
            const float dx = en[i].position.x - np.x, dz = en[i].position.z - np.z;
            if (dx * dx + dz * dz < 0.05f) { kind = (int)ex[i].kind; break; }
        }
    }
    snprintf(line, sizeof(line), "aim %s nearest %.2f kind %d (boss 3 / mob 0, -1 = none) priorityOn %.0f", tag, has ? nd : -1.0f, kind,
        m_Swarm.GetBomberParams().aimPriorityOn);
    AutoTestLog(line);
}

void AutoTestAim::Run()
{
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        Place(false, 0.0f);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 1 && m_AutoTime >= 2.5f) { Log("A"); m_AutoStep = 2; }
    if (m_AutoStep == 2 && m_AutoTime >= 3.0f) { Place(true, 2.5f); m_AutoStep = 3; }
    if (m_AutoStep == 3 && m_AutoTime >= 4.5f) { Log("B"); m_AutoStep = 4; }
    if (m_AutoStep == 4 && m_AutoTime >= 5.0f) { m_Swarm.GetBomberParams().aimPriorityOn = 0.0f; m_AutoStep = 5; }
    if (m_AutoStep == 5 && m_AutoTime >= 6.5f) { Log("C"); m_AutoStep = 6; }
    if (m_AutoStep == 6 && m_AutoTime >= 7.0f) { m_Swarm.GetBomberParams().aimPriorityOn = 1.0f; Place(true, 6.0f); m_AutoStep = 7; }
    if (m_AutoStep == 7 && m_AutoTime >= 8.5f) { Log("D"); m_AutoStep = 8; }
    if (m_AutoStep == 8 && m_AutoTime >= 9.0f) { AutoTestLog("aim done"); m_AutoStep = 9; }
}

REGISTER_BATTLE_AUTOTEST("aim", AutoTestAim)
