// ============================================================
// AutoTestPerf.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=perf
// VFXL_BATTLE_AUTOTEST=perf：野原の置物を隠す / デバッグ表示を切る段を順に回し、段毎の平均 fps を記録する
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestPerf final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 負荷の内訳（VFXL_BATTLE_AUTOTEST=perf）
// 4 秒の助走の後、8 秒毎に段を切り替え、各段の後ろ 6 秒の平均 fps・平均 / 最大フレーム ms・
// 雑魚の活き数を 1 行ずつ記録する。垂直同期は VFXL_NO_VSYNC で切っておく（上限で頭打ちになるので）。
// レベルアップの三択で止まらないよう経験値は毎フレーム 0 に戻す
// ============================================================

void AutoTestPerf::Run()
{
    // propCull: 0 = 既定の間引き / 1 = 視錐台だけ（距離で間引かない）/ 2 = 間引き無し（760 個全部）
    struct Phase { const char* name; bool hideProps; bool noDebug; int propCull; };
    static const Phase kPhases[] = {
        { "default", false, false, 0 },
        { "props hidden", true, false, 0 },
        { "default", false, false, 0 },
        { "debug draw off", false, true, 0 },
        { "props hidden + debug off", true, true, 0 },
        { "debug off, frustum cull only", false, true, 1 },
        { "debug off, no prop culling", false, true, 2 },
        { "default", false, false, 0 },
    };
    constexpr int   kPhaseCount = (int)(sizeof(kPhases) / sizeof(kPhases[0]));
    constexpr float kLead = 4.0f, kPhaseLen = 8.0f, kSettle = 2.0f;

    static bool   s_DebugDefault[3] = {};
    static StaticPropRenderer::Settings s_PropDefault;
    static int    s_Phase = -1;
    static bool   s_Done = false;
    static int    s_Frames = 0;
    static double s_SumMs = 0.0, s_MaxMs = 0.0, s_SumAlive = 0.0, s_SumProps = 0.0, s_SumSq = 0.0;
    static auto   s_Prev = std::chrono::steady_clock::now();

    const auto nowTime = std::chrono::steady_clock::now();
    const double frameMs = std::chrono::duration<double, std::milli>(nowTime - s_Prev).count();
    s_Prev = nowTime;

    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;

    char line[200];
    if (m_AutoStep == 0)
    {
        s_DebugDefault[0] = m_ShowWireframe;
        s_DebugDefault[1] = m_ShowWandDebug;
        s_DebugDefault[2] = m_ShowGridDebug;
        s_PropDefault = m_StaticProps.GetSettings();
        int models = 0;
        m_Registry.CreateView<TransformComponent, ModelComponent>()
            .Each([&](Entity, TransformComponent&, ModelComponent& mc) { if (mc.visible && mc.model && !mc.batched) ++models; });
        int decor = 0;
        SetDecorPropsVisible(true, &decor);
        snprintf(line, sizeof(line), "perf start: model entities %d + instanced props %d, colliders %d, vsync %s",
            models, decor, (int)m_CollisionSystem.GetWorldColliders().size(),
            GetEnvironmentVariableA("VFXL_NO_VSYNC", nullptr, 0) > 0 ? "off" : "on");
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (s_Done || m_AutoTime < kLead) return;

    const int phase = (int)((m_AutoTime - kLead) / kPhaseLen);
    if (phase != s_Phase)
    {
        // 前の段を締める
        if (s_Phase >= 0 && s_Frames > 0)
        {
            snprintf(line, sizeof(line), "phase %d [%s] fps %.1f avg %.2f ms sd %.2f ms max %.2f ms alive %.0f props drawn %.0f (%d frames)",
                s_Phase, kPhases[s_Phase].name, s_Frames * 1000.0 / s_SumMs, s_SumMs / s_Frames,
                std::sqrt((std::max)(0.0, s_SumSq / s_Frames - (s_SumMs / s_Frames) * (s_SumMs / s_Frames))), s_MaxMs,
                s_SumAlive / s_Frames, s_SumProps / s_Frames, s_Frames);
            AutoTestLog(line);
            // 段の最後 1 秒の CPU 内訳（FrameProfiler。1 フレームあたり ms）
            AutoTestLog(("  cpu " + FrameProfiler::Get().Summary()).c_str());
        }
        s_Frames = 0;
        s_SumMs = s_MaxMs = s_SumAlive = s_SumProps = s_SumSq = 0.0;
        s_Phase = phase;

        const bool done = phase >= kPhaseCount;
        const bool hideProps = !done && kPhases[phase].hideProps;
        const bool noDebug = !done && kPhases[phase].noDebug;
        const int propCull = done ? 0 : kPhases[phase].propCull;
        m_StaticProps.GetSettings() = s_PropDefault;
        if (propCull >= 1) m_StaticProps.GetSettings().distPerRadius = m_StaticProps.GetSettings().maxDistance = 0.0f;
        if (propCull >= 2) m_StaticProps.GetSettings().frustumCull = false;
        SetDecorPropsVisible(!hideProps, nullptr);
        m_ShowWireframe = !noDebug && s_DebugDefault[0];
        m_ShowWandDebug = !noDebug && s_DebugDefault[1];
        m_ShowGridDebug = !noDebug && s_DebugDefault[2];
        if (done)
        {
            AutoTestLog("perf done");
            s_Done = true;
        }
        return;
    }
    if (m_AutoTime - kLead - phase * kPhaseLen < kSettle) return;

    ++s_Frames;
    s_SumMs += frameMs;
    s_SumSq += frameMs * frameMs;
    s_MaxMs = (std::max)(s_MaxMs, frameMs);
    s_SumAlive += m_Swarm.GetCounters().aliveEnemies;
    s_SumProps += m_StaticProps.GetStats().drawn;
}

REGISTER_BATTLE_AUTOTEST("perf", AutoTestPerf)
