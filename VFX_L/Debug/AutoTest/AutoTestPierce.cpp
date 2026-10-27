// ============================================================
// AutoTestPierce.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=pierce
// 黄金の矢の貫通（GoldenArrow.json の pierce 3、2026-10-28）：正面の一直線に動かない的 5 体（6 / 8 / 10 / 12 / 14m、HP 10 万）、
// バックパックは黄金の矢だけ。3 秒撃って、近い順に受けた HP と 1 体目に対する割合を
// `pierce result lost <5 体分> ratio <5 体分>` に出す（期待：1 / 0.8 / 0.64 / 0.51 / 0 = 4 体目まで当たり 5 体目は無傷）。
// 1.6 秒目に横から `pierce look line`
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestPierce final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        Vector3 m_Origin;
        bool m_Shot = false;
    };

    constexpr float kTargetHp = 100000.0f;
    constexpr float kDist[5] = { 6.0f, 8.0f, 10.0f, 12.0f, 14.0f };
}

void AutoTestPierce::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    char line[256];

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        ClearBackpackItems(bp);
        const int lo = BackpackComponent::GRID / 2 - 1;
        if (BackpackLogic::Place(bp, ItemID::GoldenArrow, lo, lo + 1, 0) < 0) AutoTestLog("pierce place FAILED");
        bp.dirty = true;
        m_Origin = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float d : kDist)
        {
            const float x = m_Origin.x, z = m_Origin.z + d;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), kTargetHp, 0.0f);
        }
        auto& cam = m_Camera.Camera();
        cam.SetYaw(-90.0f);   // 横から見る（列が画面の左右に並ぶ）
        cam.SetPitch(25.0f);
        cam.distance = 14.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("pierce start");
        m_AutoStep = 1;
    }
    if (m_AutoStep == 1 && !m_Shot && m_AutoTime >= 2.6f) { AutoTestLog("pierce look line"); m_Shot = true; }
    if (m_AutoStep == 1 && m_AutoTime >= 4.0f)
    {
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        float lost[5] = {};
        if (m_Swarm.DebugReadEnemies(en, st))
        {
            for (size_t i = 0; i < en.size() && i < st.size(); ++i)
            {
                if (st[i] == Swarm::kStateDead) continue;
                const float dz = en[i].position.z - m_Origin.z;
                for (int k = 0; k < 5; ++k)
                    if (std::abs(dz - kDist[k]) < 0.8f && std::abs(en[i].position.x - m_Origin.x) < 0.8f)
                        lost[k] = kTargetHp - Swarm::HpFromFixed(en[i].hp);
            }
        }
        const float first = (std::max)(lost[0], 1.0f);
        snprintf(line, sizeof(line), "pierce result lost %.0f %.0f %.0f %.0f %.0f ratio %.2f %.2f %.2f %.2f %.2f",
            lost[0], lost[1], lost[2], lost[3], lost[4],
            lost[0] / first, lost[1] / first, lost[2] / first, lost[3] / first, lost[4] / first);
        AutoTestLog(line);
        AutoTestLog("pierce done");
        m_AutoStep = 2;
    }
}

REGISTER_BATTLE_AUTOTEST("pierce", AutoTestPierce)
