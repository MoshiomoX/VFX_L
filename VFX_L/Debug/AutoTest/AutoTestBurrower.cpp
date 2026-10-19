// ============================================================
// AutoTestBurrower.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=burrower
// 砂潜り（kEnemyKindBurrower。2026-10-17）：正面 12m に 1 体。砂の下を寄って来て、足元で顔を出して 1 回噛み、また潜る（ユーザー「冒头一咬就走」）。
// 潜っている間は撃っても当たらない（HitCS が飛ばす）ことも見る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBurrower final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// 1 秒: 湧き停止・全消し・HP 10 万・詠唱停止。正面 +Z 12m に砂潜り 1 体、カメラ背後 9m / 24°
// 1.5 秒: `burrower look buried`（砂の山だけ）。0.25 秒毎に `burrower t fuse dist hp hits`（fuse > 0 = 顔を出している）、
//   初めて顔を出した 0.35 秒後に `burrower look surfaced`。6 秒: 詠唱を戻す（潜っている時は当たらない）。
//   10 秒: `burrower fight alive` + `look fight`。11 秒: done
// ============================================================
void AutoTestBurrower::Run()
{
    char line[200];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    static float s_NextLog = 1.0f, s_LastHp = 0.0f, s_SurfacedAt = -1.0f;
    static int s_Hits = 0;
    static bool s_LookSurf = false, s_LookBuried = false;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        if (m_Registry.Has<HealthComponent>(m_Player))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.max = hp.current = 100000.0f;
            s_LastHp = hp.current;
        }
        const float groundY = m_Swarm.GetAIParams().groundY;
        float hp = 15.0f, speed = 3.5f;
        m_Mobs.KindStats(Swarm::kEnemyKindBurrower, hp, speed);
        const float x = tf.position.x, z = tf.position.z + 12.0f;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), hp, speed, Swarm::kEnemyKindBurrower);
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(24.0f);
        cam.distance = 9.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "burrower start hp %.0f speed %.2f", hp, speed);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    if (!s_LookBuried && m_AutoTime >= 1.5f) { AutoTestLog("burrower look buried"); s_LookBuried = true; }

    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        if (hp.current < s_LastHp - 0.5f) ++s_Hits;
        s_LastHp = hp.current;
    }
    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog += 0.25f;
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        std::vector<Swarm::EnemyExtra> ex;
        if (m_Swarm.DebugReadEnemies(en, st, &ex))
        {
            for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
            {
                if (st[i] == Swarm::kStateDead || ex[i].kind != Swarm::kEnemyKindBurrower) continue;
                const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
                snprintf(line, sizeof(line), "burrower t %.2f fuse %.2f dist %.2f hp %.1f hits %d", m_AutoTime, ex[i].fuse,
                    std::sqrt(dx * dx + dz * dz), Swarm::HpFromFixed(en[i].hp), s_Hits);
                AutoTestLog(line);
                if (ex[i].fuse > 0.0f && s_SurfacedAt < 0.0f) s_SurfacedAt = m_AutoTime;
                break;
            }
        }
    }
    if (!s_LookSurf && s_SurfacedAt >= 0.0f && m_AutoTime >= s_SurfacedAt + 0.35f) { AutoTestLog("burrower look surfaced"); s_LookSurf = true; }
    if (m_AutoStep == 1 && m_AutoTime >= 6.0f)
    {
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        m_AutoStep = 2;
    }
    if (m_AutoStep == 2 && m_AutoTime >= 10.0f)
    {
        snprintf(line, sizeof(line), "burrower fight alive %u hits %d", m_Swarm.GetCounters().aliveEnemies, s_Hits);
        AutoTestLog(line);
        AutoTestLog("burrower look fight");
        m_AutoStep = 3;
    }
    if (m_AutoStep == 3 && m_AutoTime >= 11.0f) { AutoTestLog("burrower done"); m_AutoStep = 4; }
}

REGISTER_BATTLE_AUTOTEST("burrower", AutoTestBurrower)
