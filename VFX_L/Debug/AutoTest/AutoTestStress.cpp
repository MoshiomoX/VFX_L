// ============================================================
// AutoTestStress.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=stress
// VFXL_BATTLE_AUTOTEST=stress：プレイヤーは動かず無敵、魔法をバックパックに置いて撃たせ、雑魚の数を段ごとに増やす
// （0 / 250 / 500 / 1000 / 2000 / 4000 / 4000 + 弾 2000）。段毎の fps と CPU / GPU の内訳を記録する
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestStress final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 負荷試験（VFXL_BATTLE_AUTOTEST=stress）
// プレイヤーは動かず無敵（HP 1e6）、魔力も無限。開始時の 3x3 枠へ火球・弧・追尾・隕石を置けるだけ置いて撃たせる。
// 湧きは SpawnDirector の上限を段ごとに上げ、10〜30m に一気に湧かせる（倒されても上限まで補充）。
// 段 12 秒、最初の 6 秒は寄って来るのを待つ。段の後半の平均 fps・最長フレーム・活き数と、
// 最後 1 秒の CPU / GPU の内訳（FrameProfiler）を記録。三択で止まらないよう経験値は毎フレーム 0
// ============================================================
void AutoTestStress::Run()
{
    struct Phase { const char* name; int mobs; int shots; };
    static const Phase kPhases[] = {
        { "0 mobs", 0, 0 },
        { "250 mobs", 250, 0 },
        { "500 mobs", 500, 0 },
        { "1000 mobs", 1000, 0 },
        { "2000 mobs", 2000, 0 },
        { "4000 mobs", 4000, 0 },
        { "4000 mobs + 2000 shots", 4000, 2000 },
    };
    constexpr int   kPhaseCount = (int)(sizeof(kPhases) / sizeof(kPhases[0]));
    constexpr float kLead = 3.0f, kPhaseLen = 12.0f, kSettle = 6.0f;

    static int    s_Phase = -1;
    static bool   s_Done = false;
    static int    s_Frames = 0;
    static double s_SumMs = 0.0, s_MaxMs = 0.0;
    static double s_Sum[4] = {};   // 雑魚 / 弾 / 経験値オーブ / 範囲
    static auto   s_Prev = std::chrono::steady_clock::now();

    const auto nowTime = std::chrono::steady_clock::now();
    const double frameMs = std::chrono::duration<double, std::milli>(nowTime - s_Prev).count();
    s_Prev = nowTime;

    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }

    char line[240];
    if (m_AutoStep == 0)
    {
        // 魔法を開始時の枠（3..5 行・列）へ置けるだけ置く
        int placed = 0;
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const ItemID spells[] = { ItemID::Fireball, ItemID::ArcBolt, ItemID::HomingBolt, ItemID::Meteor };
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            for (ItemID id : spells)
            {
                bool done = false;
                for (int r = lo; r < lo + 3 && !done; ++r)
                    for (int c = lo; c < lo + 3 && !done; ++c)
                        if (BackpackLogic::CanPlace(bp, id, r, c, 0))
                        {
                            BackpackLogic::Place(bp, id, r, c, 0);
                            done = true;
                            ++placed;
                        }
            }
            bp.dirty = true;
        }
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        auto& d = m_Mobs.Director();
        d.enabled = true;
        d.spawnCap = 0;
        m_Mobs.scaling = false;   // 難度の倍率・湧く速さの曲線は切る（数だけ段毎に変える）
        d.spawnPerSecond = 2000.0f;
        d.maxPerFrame = 100;
        d.rMin = 10.0f;
        d.rMax = 30.0f;
        snprintf(line, sizeof(line), "stress start: %d spells placed, vsync %s", placed,
            GetEnvironmentVariableA("VFXL_NO_VSYNC", nullptr, 0) > 0 ? "off" : "on");
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (s_Done || m_AutoTime < kLead) return;

    const int phase = (int)((m_AutoTime - kLead) / kPhaseLen);
    if (phase != s_Phase)
    {
        if (s_Phase >= 0 && s_Frames > 0)
        {
            snprintf(line, sizeof(line),
                "stress %d [%s] fps %.1f avg %.2f ms max %.2f ms | mobs %.0f shots %.0f orbs %.0f areas %.0f (%d frames)",
                s_Phase, kPhases[s_Phase].name, s_Frames * 1000.0 / s_SumMs, s_SumMs / s_Frames, s_MaxMs,
                s_Sum[0] / s_Frames, s_Sum[1] / s_Frames, s_Sum[2] / s_Frames, s_Sum[3] / s_Frames, s_Frames);
            AutoTestLog(line);
            AutoTestLog(("  prof " + FrameProfiler::Get().Summary()).c_str());
        }
        s_Frames = 0;
        s_SumMs = s_MaxMs = 0.0;
        for (double& v : s_Sum) v = 0.0;
        s_Phase = phase;

        if (phase >= kPhaseCount)
        {
            m_Mobs.Director().spawnCap = 0;
            m_Stress.SetAutoRefill(false, 0, 0);
            AutoTestLog("stress done");
            s_Done = true;
            return;
        }
        m_Mobs.Director().spawnCap = kPhases[phase].mobs;
        m_Stress.SetAutoRefill(kPhases[phase].shots > 0, kPhases[phase].shots, 100);
        return;
    }
    if (m_AutoTime - kLead - phase * kPhaseLen < kSettle) return;

    const auto& c = m_Swarm.GetCounters();
    ++s_Frames;
    s_SumMs += frameMs;
    s_MaxMs = (std::max)(s_MaxMs, frameMs);
    s_Sum[0] += c.aliveEnemies;
    s_Sum[1] += c.aliveProjectiles;
    s_Sum[2] += c.aliveOrbs;
    s_Sum[3] += c.aliveAreas;
}

REGISTER_BATTLE_AUTOTEST("stress", AutoTestStress)
