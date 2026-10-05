// ============================================================
// AutoTestPoisonPerf.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=poisonperf
// 毒沼を大量に出した時の負荷（2026-10-05）。正面にほとんど動かない雑魚を置き、毒の発動間隔を
// 段ごとに縮めて（撃たない / 0.4 秒 / 0.1 秒 / 0.1 秒で液面を描かない）、段の後半の平均フレーム時間・
// 範囲の数・FrameProfiler の内訳を記録する。垂直同期は VFXL_NO_VSYNC で切っておく
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestPoisonPerf final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Setup();
        void Report();

        struct Phase { const char* name; float interval; bool liquids; };   // interval <= 0 = 撃たない
        static constexpr Phase kPhases[] = {
            { "no cast", 0.0f, true },
            { "cast 0.4s", 0.4f, true },
            { "cast 0.1s", 0.1f, true },
            { "cast 0.1s, liquids off", 0.1f, false },
        };
        static constexpr int   kPhaseCount = (int)(sizeof(kPhases) / sizeof(kPhases[0]));
        static constexpr float kLead = 2.0f, kPhaseLen = 11.0f, kSettle = 6.0f;

        int    m_Phase = -1;
        bool   m_Done = false;
        int    m_Frames = 0;
        double m_SumMs = 0.0, m_MaxMs = 0.0, m_SumAreas = 0.0, m_SumShots = 0.0;
        std::chrono::steady_clock::time_point m_Prev = std::chrono::steady_clock::now();
    };
}

void AutoTestPoisonPerf::Setup()
{
    m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
    m_Mobs.Director().enabled = false;
    m_Swarm.KillAll();
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
    const float gy = m_Swarm.GetAIParams().groundY;
    // 正面 5〜9m に倒れない雑魚 12 体（毒は一番近い敵の足元に落ちる = 普段と同じく同じ辺りに重なる）
    for (int k = 0; k < 12; ++k)
        m_Swarm.SpawnEnemy(Vector3(pp.x + ((k % 3) - 1) * 1.6f, gy, pp.z + 5.0f + (float)(k / 3) * 1.3f), 1.0e6f, 0.2f);
    if (m_Registry.Has<BackpackComponent>(m_Player))
    {
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        const int lo = BackpackComponent::GRID / 2 - 1;
        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, ItemID::Poison, lo + 1, lo + 1, 0);
        bp.dirty = true;
    }
    m_Camera.Camera().SetYaw(0.0f);
    m_Camera.Camera().SnapToTarget();
    char line[160];
    snprintf(line, sizeof(line), "poisonperf start: 12 mobs at +Z 5-9m, vsync %s",
        GetEnvironmentVariableA("VFXL_NO_VSYNC", nullptr, 0) > 0 ? "off" : "on");
    AutoTestLog(line);
}

void AutoTestPoisonPerf::Report()
{
    if (m_Phase < 0 || m_Frames <= 0) return;
    char line[240];
    snprintf(line, sizeof(line), "poisonperf %d [%s] fps %.1f avg %.2f ms max %.2f ms | areas %.1f shots %.1f (%d frames)",
        m_Phase, kPhases[m_Phase].name, m_Frames * 1000.0 / m_SumMs, m_SumMs / m_Frames, m_MaxMs,
        m_SumAreas / m_Frames, m_SumShots / m_Frames, m_Frames);
    AutoTestLog(line);
    AutoTestLog(("  prof " + FrameProfiler::Get().Summary()).c_str());
}

void AutoTestPoisonPerf::Run()
{
    const auto now = std::chrono::steady_clock::now();
    const double frameMs = std::chrono::duration<double, std::milli>(now - m_Prev).count();
    m_Prev = now;

    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;

    if (m_AutoStep == 0)
    {
        if (m_AutoTime < 1.0f) return;
        Setup();
        m_AutoStep = 1;
    }
    if (m_Done || m_AutoTime < kLead || !m_Registry.Has<WandComponent>(m_Player)) return;

    const int phase = (int)((m_AutoTime - kLead) / kPhaseLen);
    if (phase != m_Phase)
    {
        Report();
        m_Frames = 0;
        m_SumMs = m_MaxMs = m_SumAreas = m_SumShots = 0.0;
        m_Phase = phase;
        if (phase >= kPhaseCount)
        {
            m_Swarm.liquids = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
            AutoTestLog("poisonperf done");
            m_Done = true;
            return;
        }
        m_Swarm.liquids = kPhases[phase].liquids;
    }

    // 発動間隔は集約のたびに元へ戻るので毎フレーム上書きする
    auto& wand = m_Registry.Get<WandComponent>(m_Player);
    wand.castingPaused = kPhases[phase].interval <= 0.0f;
    for (auto& s : wand.spells)
        if (kPhases[phase].interval > 0.0f) s.castInterval = kPhases[phase].interval;

    if (m_AutoTime - kLead - phase * kPhaseLen < kSettle) return;
    const auto& c = m_Swarm.GetCounters();
    ++m_Frames;
    m_SumMs += frameMs;
    m_MaxMs = (std::max)(m_MaxMs, frameMs);
    m_SumAreas += c.aliveAreas;
    m_SumShots += c.aliveProjectiles;
}

REGISTER_BATTLE_AUTOTEST("poisonperf", AutoTestPoisonPerf)
