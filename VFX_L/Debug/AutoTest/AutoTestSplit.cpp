// ============================================================
// AutoTestSplit.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=split
// VFXL_BATTLE_AUTOTEST=split：スプリッターを正面に出して倒し、分裂体が出るか・種類毎の数・湧きの割合
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestSplit final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: スプリッター（VFXL_BATTLE_AUTOTEST=split、2026-10-03）
// 1 秒: 湧き停止・全消し・無敵・MP 無限、バックパックは追尾弾（中）+ 火球（左上）。カメラはプレイヤーの背後 11m / 32°、+Z 向き。
// 1.5 / 5 / 8.5 秒に正面（+Z）7〜9m へスプリッター 6 体。0.25 秒毎に池を読み戻して
// `split t splitters splitlings others kills events spawned`。分裂体が初めて見えた 0.15 秒後・2.5 秒・6 秒に
// `split look <n>`。12 秒: 詠唱を止め、計時を 470 秒に固定して湧きを戻す（第 1 面のスプリッターの割合 ≒ 0.25）。
// 18 秒に種類毎の数を `split mix ...`、`split done`
// ============================================================
void AutoTestSplit::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mana = m_Registry.Get<ManaComponent>(m_Player);
        mana.current = mana.max;
    }
    auto& cam = m_Camera.Camera();
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
    char line[300];
    static float s_NextLog = 0.0f;
    static int s_Wave = 0;
    static bool s_SawSplitling = false;
    static float s_ShotAt = -1.0f;
    static int s_Shot = 0;

    auto spawnWave = [&]()
        {
            const float gy = m_Swarm.GetAIParams().groundY;
            for (int i = 0; i < 6; ++i)
            {
                const float x = pp.x - 5.0f + 2.0f * (float)i;
                const float z = pp.z + 7.0f + (float)(i % 2) * 2.0f;
                m_Swarm.SpawnEnemy({ x, m_Grid.SampleHeight(x, z) + gy, z }, 30.0f, 3.2f, Swarm::kEnemyKindSplitter);
            }
            ++s_Wave;
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::HomingBolt, lo + 1, lo + 1, 0);
            BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
            bp.dirty = true;
        }
        cam.SetYaw(0.0f);
        cam.distance = 11.0f;
        cam.SetPitch(32.0f);
        cam.SnapToTarget();
        s_NextLog = m_AutoTime;
        s_Wave = 0;
        s_SawSplitling = false;
        s_ShotAt = -1.0f;
        s_Shot = 0;
        m_AutoStep = 1;
    }
    if (m_AutoStep == 1)
    {
        const float t = m_AutoTime - 1.0f;
        if ((s_Wave == 0 && t >= 0.5f) || (s_Wave == 1 && t >= 4.0f) || (s_Wave == 2 && t >= 7.5f)) spawnWave();
        if (m_AutoTime >= s_NextLog)
        {
            s_NextLog += 0.25f;
            std::vector<Swarm::Enemy> en;
            std::vector<uint32_t> st;
            std::vector<Swarm::EnemyExtra> ex;
            if (m_Swarm.DebugReadEnemies(en, st, &ex))
            {
                int splitters = 0, splitlings = 0, others = 0;
                float nearest = 1.0e9f;
                for (size_t i = 0; i < st.size() && i < ex.size(); ++i)
                {
                    if (st[i] == Swarm::kStateDead) continue;
                    if (ex[i].kind == Swarm::kEnemyKindSplitter) ++splitters;
                    else if (ex[i].kind == Swarm::kEnemyKindSplitling)
                    {
                        ++splitlings;
                        nearest = (std::min)(nearest, Vector3(en[i].position.x - pp.x, 0.0f, en[i].position.z - pp.z).Length());
                    }
                    else ++others;
                }
                snprintf(line, sizeof(line), "split t %.2f splitters %d splitlings %d others %d kills %u events %u spawned %u nearestSplitling %.1f",
                    t, splitters, splitlings, others, m_Swarm.GetCounters().killCount, m_Mobs.GetSplitEventsSeen(),
                    m_Mobs.GetSplitlingsSpawned(), splitlings > 0 ? nearest : -1.0f);
                AutoTestLog(line);
                if (splitlings > 0 && !s_SawSplitling) { s_SawSplitling = true; s_ShotAt = m_AutoTime + 0.15f; }
            }
        }
        if (s_ShotAt > 0.0f && m_AutoTime >= s_ShotAt)
        {
            snprintf(line, sizeof(line), "split look %d", s_Shot++);
            AutoTestLog(line);
            s_ShotAt = -1.0f;
        }
        if ((s_Shot == 1 && t >= 2.5f) || (s_Shot == 2 && t >= 6.0f))
        {
            snprintf(line, sizeof(line), "split look %d", s_Shot++);
            AutoTestLog(line);
        }
        if (t >= 11.0f)
        {
            // 湧きの割合: 詠唱を止めて計時を 470 秒に（スプリッターの割合 ≒ 0.25、自爆兵 0.15）
            m_Swarm.KillAll();
            if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
            m_Mobs.Director().enabled = true;
            m_AutoStep = 2;
            s_NextLog = m_AutoTime + 6.0f;
        }
    }
    if (m_AutoStep == 2)
    {
        m_RunTime = 470.0f;
        if (m_AutoTime >= s_NextLog)
        {
            std::vector<Swarm::Enemy> en;
            std::vector<uint32_t> st;
            std::vector<Swarm::EnemyExtra> ex;
            if (m_Swarm.DebugReadEnemies(en, st, &ex))
            {
                int count[8] = {};
                for (size_t i = 0; i < st.size() && i < ex.size(); ++i)
                    if (st[i] != Swarm::kStateDead && ex[i].kind < 8) ++count[ex[i].kind];
                const int total = count[0] + count[1] + count[5];
                snprintf(line, sizeof(line), "split mix ratioNow %.3f mobs %d bombers %d splitters %d splitlings %d elites %d -> splitter share %.3f bomber share %.3f",
                    m_Mobs.GetSplitterRatio(), count[0], count[1], count[5], count[6], count[2],
                    total > 0 ? (float)count[5] / total : 0.0f, total > 0 ? (float)count[1] / total : 0.0f);
                AutoTestLog(line);
            }
            AutoTestLog("split done");
            m_AutoStep = 3;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("split", AutoTestSplit)
