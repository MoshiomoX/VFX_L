// ============================================================
// AutoTestStressFx.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=stressfx（草原 VFXL_STAGE=1 で。Demo ビルドでも回せる：DevUI::AutoTestAllowed）
// 負荷試験（2026-10-23、ユーザー：「敵の数、新しく入れた風まわり、点光源を中心に」）。段ごとに足していく：
//   0 何も無し / 1 風だけ（竜巻の風源 4 つがプレイヤーの周りを回る + 竜巻の見た目 Twister.json）/
//   2 点光源 192 個だけ / 3 雑魚 1000 / 4 雑魚 4000 / 5 雑魚 4000 + 風 / 6 + 点光源 / 7 + 詠唱（弾・命中・弾の光）
// 風 = 草の巻き上げ・渦の跡（GrassCullCS / GrassTrampleCS）・草屑（GrassShreds）・落ち葉・粒子・マントの局所の風。
// 点光源は 192 個（上限 256 のうち、詠唱の段で弾の光が載る分を残す）。カメラは普段の背後 14m / 45°。
// 段 12 秒、前の 6 秒は雑魚が寄ってくるのを待ち、後の 6 秒の平均 / 最長フレームと FrameProfiler の内訳を書く。
// 垂直同期は VFXL_NO_VSYNC で切る。地形は VFXL_TERRAIN_SEED で固定する
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Graphics/Light/PointLightManager.h"
#include "Core/DevUI.h"
#include "Swarm/AreaVFXPlayer.h"

namespace
{
    class AutoTestStressFx final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Setup();
        void Report();
        void UpdateWind(bool on);
        void AddLights(int count);

        struct Phase { const char* name; int mobs; bool wind; int lights; bool cast; };
        static constexpr Phase kPhases[] = {
            { "baseline",                          0, false,   0, false },
            { "wind x4",                           0, true,    0, false },
            { "lights 192",                        0, false, 192, false },
            { "1000 mobs",                      1000, false,   0, false },
            { "4000 mobs",                      4000, false,   0, false },
            { "4000 mobs + wind",               4000, true,    0, false },
            { "4000 mobs + wind + lights",      4000, true,  192, false },
            { "4000 mobs + wind + lights + cast", 4000, true, 192, true },
        };
        static constexpr int   kPhaseCount = (int)(sizeof(kPhases) / sizeof(kPhases[0]));
        static constexpr float kLead = 3.0f, kPhaseLen = 12.0f, kSettle = 6.0f;
        static constexpr int   kWinds = 4;
        static constexpr int   kGrid = 16;   // 点光源の置き場所 16×16

        Vector3  m_Center;
        uint32_t m_TwisterVfx[kWinds] = {};
        int      m_Order[kGrid * kGrid] = {};
        int      m_Phase = -1;
        bool     m_Done = false;
        bool     m_Looked = false;   // 段ごとに 1 回「stressfx look p<n>」（外部のスクリプトが撮る。2026-10-23）
        int      m_Frames = 0;
        double   m_SumMs = 0.0, m_MaxMs = 0.0;
        double   m_Sum[6] = {};   // 雑魚 / 弾 / オーブ / 範囲 / CPU の光 / 草屑の発生源
        std::chrono::steady_clock::time_point m_Prev = std::chrono::steady_clock::now();
    };
}

void AutoTestStressFx::Setup()
{
    m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
    m_Center = m_Registry.Get<TransformComponent>(m_Player).position;

    // 詠唱の段で撃つ基本魔法（9×9 の枠のどこかへ置けるだけ）。それまでは止めておく
    int placed = 0;
    if (m_Registry.Has<BackpackComponent>(m_Player))
    {
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        ClearBackpackItems(bp);
        FillAllFrames(bp);
        const ItemID spells[] = { ItemID::Fireball, ItemID::ArcBolt, ItemID::HomingBolt,
                                  ItemID::StoneShot, ItemID::IceLance, ItemID::GoldenArrow };
        for (ItemID id : spells)
        {
            bool done = false;
            for (int r = 0; r < BackpackComponent::GRID && !done; ++r)
                for (int c = 0; c < BackpackComponent::GRID && !done; ++c)
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
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;

    // 湧き：段ごとに上限だけ変え、10〜30m に一気に（stress と同じ）
    auto& d = m_Mobs.Director();
    d.enabled = true;
    d.spawnCap = 0;
    m_Mobs.scaling = false;
    d.spawnPerSecond = 2000.0f;
    d.maxPerFrame = 100;
    d.rMin = 10.0f;
    d.rMax = 30.0f;

    // 点光源の置き場所の順番（決まった種で混ぜる）
    for (int i = 0; i < kGrid * kGrid; ++i) m_Order[i] = i;
    uint32_t s = 12345u;
    for (int i = kGrid * kGrid - 1; i > 0; --i)
    {
        s = s * 1664525u + 1013904223u;
        std::swap(m_Order[i], m_Order[(s >> 8) % (uint32_t)(i + 1)]);
    }

    const auto& gfx = Application::Get().GetGraphics();
    char line[220];
    snprintf(line, sizeof(line), "stressfx start: %s build, screen %.0fx%.0f, vsync %s, stage %d, %d spells, clusters %s",
        DevUI::kEnabled ? "dev" : "demo", gfx.GetWidth(), gfx.GetHeight(),
        GetEnvironmentVariableA("VFXL_NO_VSYNC", nullptr, 0) > 0 ? "off" : "on", m_StageIndex, placed,
        PointLightManager::Get().Clusters().GetSettings().enabled ? "on" : "off");
    AutoTestLog(line);
}

// 竜巻の風源 4 つ：プレイヤーの周り 9m の輪を回る（毎フレーム出し直す風源 + 竜巻の見た目を動かす）
void AutoTestStressFx::UpdateWind(bool on)
{
    for (int i = 0; i < kWinds; ++i)
    {
        if (!on)
        {
            if (m_TwisterVfx[i] != 0) { m_AreaVFX.StopInstance(m_TwisterVfx[i]); m_TwisterVfx[i] = 0; }
            continue;
        }
        const float a = m_AutoTime * 0.35f + (float)i * DirectX::XM_PIDIV2;
        const float r = 9.0f + 2.0f * std::sin(m_AutoTime * 0.7f + (float)i);
        Vector3 p = m_Center + Vector3(std::cos(a) * r, 0.0f, std::sin(a) * r);
        p.y = m_Grid.SampleHeight(p.x, p.z);

        Swarm::WindSource w = {};
        w.pos[0] = p.x; w.pos[1] = p.y; w.pos[2] = p.z;
        w.radius = 6.0f;      // 竜巻（半径 2.8 × 2.2）と同じくらい
        w.swirl = 14.0f;
        w.updraft = 5.0f;
        w.inflow = 3.0f;
        w.fade = 1.0f;
        m_Swarm.AddFrameWindSource(w);

        if (m_TwisterVfx[i] == 0)
            m_TwisterVfx[i] = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::Twister), p, 3600.0f, false, m_VFXContext);
        else
            m_AreaVFX.SetInstance(m_TwisterVfx[i], p, p);
    }
}

// 画面内（プレイヤーの横 ±14m・前 -4〜22m）に 16×16 の置き場所から count 個（光の表は毎フレーム空になる）
void AutoTestStressFx::AddLights(int count)
{
    static const Vector3 kColors[] = {
        { 1.0f, 0.55f, 0.20f }, { 0.35f, 0.65f, 1.0f }, { 0.45f, 1.0f, 0.40f },
        { 1.0f, 0.35f, 0.75f }, { 0.95f, 0.90f, 0.40f }, { 0.60f, 0.40f, 1.0f },
    };
    auto& lights = PointLightManager::Get();
    count = (std::min)(count, kGrid * kGrid);
    for (int k = 0; k < count; ++k)
    {
        const int cell = m_Order[k];
        const float x = m_Center.x - 14.0f + 28.0f * (float)(cell % kGrid) / (kGrid - 1);
        const float z = m_Center.z - 4.0f + 26.0f * (float)(cell / kGrid) / (kGrid - 1);
        lights.Add(Vector3(x, m_Grid.SampleHeight(x, z) + 1.2f, z), kColors[k % 6], 3.5f, 1.0f);
    }
}

void AutoTestStressFx::Report()
{
    if (m_Phase < 0 || m_Frames <= 0) return;
    const double n = (double)m_Frames;
    char line[300];
    snprintf(line, sizeof(line),
        "stressfx %d [%s] fps %.1f avg %.2f ms max %.2f ms | mobs %.0f shots %.0f orbs %.0f areas %.0f cpuLights %.0f shreds %.1f (%d frames)",
        m_Phase, kPhases[m_Phase].name, n * 1000.0 / m_SumMs, m_SumMs / n, m_MaxMs,
        m_Sum[0] / n, m_Sum[1] / n, m_Sum[2] / n, m_Sum[3] / n, m_Sum[4] / n, m_Sum[5] / n, m_Frames);
    AutoTestLog(line);
    AutoTestLog(("  prof " + FrameProfiler::Get().Summary()).c_str());
}

void AutoTestStressFx::Run()
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
    if (m_Done || m_AutoTime < kLead) return;

    const int phase = (int)((m_AutoTime - kLead) / kPhaseLen);
    if (phase != m_Phase)
    {
        Report();
        m_Frames = 0;
        m_SumMs = m_MaxMs = 0.0;
        for (double& v : m_Sum) v = 0.0;
        m_Phase = phase;
        m_Looked = false;
        if (phase >= kPhaseCount)
        {
            UpdateWind(false);
            m_Mobs.Director().spawnCap = 0;
            if (m_Registry.Has<WandComponent>(m_Player))
                m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
            AutoTestLog("stressfx done");
            m_Done = true;
            return;
        }
        m_Mobs.Director().spawnCap = kPhases[phase].mobs;
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = !kPhases[phase].cast;
    }

    const Phase& ph = kPhases[phase];
    UpdateWind(ph.wind);
    AddLights(ph.lights);

    if (m_AutoTime - kLead - phase * kPhaseLen < kSettle) return;
    if (!m_Looked && m_AutoTime - kLead - phase * kPhaseLen >= kSettle + 3.0f)
    {
        m_Looked = true;
        char look[32];
        snprintf(look, sizeof(look), "stressfx look p%d", phase);
        AutoTestLog(look);
    }
    const auto& c = m_Swarm.GetCounters();
    ++m_Frames;
    m_SumMs += frameMs;
    m_MaxMs = (std::max)(m_MaxMs, frameMs);
    m_Sum[0] += c.aliveEnemies;
    m_Sum[1] += c.aliveProjectiles;
    m_Sum[2] += c.aliveOrbs;
    m_Sum[3] += c.aliveAreas;
    m_Sum[4] += PointLightManager::Get().GetCpuCount();
    m_Sum[5] += m_GrassShreds.ActiveCount();
}

REGISTER_BATTLE_AUTOTEST("stressfx", AutoTestStressFx)
