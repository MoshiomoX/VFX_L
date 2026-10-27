// ============================================================
// AutoTestGrassFx.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=grassfx（草原 VFXL_STAGE=1 で）
// 草地の細部（2026-10-23）を撮る：
//   A  竜巻の風源（CPU の AddFrameWindSource）が正面 7m を左から右へ 7 秒で横切る：葉が流れの向きへ寝て、
//      芯の近くで先が巻き上がる / 草屑が巻かれる（look core1〜3）。止めた後に残る渦の跡（look trail1 / 2）
//   B  世界の風を 3 倍に（GrassRenderer::debugWindMul）：草の波と裏返り（look wind1 / 2）
//   C  カメラを寄せて葉の形（4 段・ねじれ・枯れ葉。look close）、正面 3m に止まった風源（look closecurl）
// カメラは毎フレーム固定（ユーザーがマウスを動かしても回らない）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestGrassFx final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Look(const char* name);
        void AddSource(const Vector3& pos, float radius, float fade);

        Vector3 m_Center;
        int m_Next = 0;      // 次に撮る番号（kLooks）
        bool m_Done = false;
    };

    struct LookAt { float time; const char* name; };
    constexpr LookAt kLooks[] = {
        { 3.5f, "core1" }, { 5.5f, "core2" }, { 7.5f, "core3" },
        { 9.3f, "trail1" }, { 10.6f, "trail2" },
        { 13.0f, "wind1" }, { 14.2f, "wind2" },
        { 17.5f, "close" }, { 19.6f, "closecurl" },
    };
}

void AutoTestGrassFx::Look(const char* name)
{
    char line[160];
    snprintf(line, sizeof(line), "grassfx look %s blades %u shreds %d windMul %.1f",
        name, m_Grass.LastBladeCount(), m_GrassShreds.ActiveCount(), m_Grass.GetSettings().debugWindMul);
    AutoTestLog(line);
    AutoTestLog(("  prof " + FrameProfiler::Get().Summary()).c_str());   // 草の GPU（Release で見る）
}

// 竜巻と同じくらいの風源（WeaponSystem::twisterLook：旋回 14 / 上昇 5 / 吸い込み 3）
void AutoTestGrassFx::AddSource(const Vector3& pos, float radius, float fade)
{
    Swarm::WindSource w = {};
    w.pos[0] = pos.x; w.pos[1] = m_Grid.SampleHeight(pos.x, pos.z); w.pos[2] = pos.z;
    w.radius = radius;
    w.swirl = 14.0f;
    w.updraft = 5.0f;
    w.inflow = 3.0f;
    w.fade = std::clamp(fade, 0.0f, 1.0f);
    m_Swarm.AddFrameWindSource(w);
}

void AutoTestGrassFx::Run()
{
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;

    if (m_AutoStep == 0)
    {
        if (m_AutoTime < 1.0f) return;
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_Center = m_Registry.Get<TransformComponent>(m_Player).position;
        char line[120];
        snprintf(line, sizeof(line), "grassfx start stage %d grass %d", m_StageIndex, m_Grass.GetSettings().enabled ? 1 : 0);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (m_Done) return;

    // カメラ：A / B は後ろ 8m・22°、C は 3.5m・12°（毎フレーム固定）
    const bool close = m_AutoTime >= 16.5f;
    auto& cam = m_Camera.Camera();
    cam.SetYaw(0.0f);
    cam.SetPitch(close ? 12.0f : 22.0f);
    cam.distance = close ? 3.5f : 8.0f;
    cam.avoidOcclusion = false;
    if (m_AutoTime < 1.2f || (close && m_AutoTime < 16.6f)) cam.SnapToTarget();

    // A: 竜巻の風源が正面 7m を左から右へ（2〜9 秒、出入り 0.5 秒）
    const float t = m_AutoTime;
    if (t >= 2.0f && t < 9.0f)
    {
        const float a = (t - 2.0f) / 7.0f;
        const Vector3 p = m_Center + Vector3(-6.0f + 12.0f * a, 0.0f, 7.0f);
        AddSource(p, 5.0f, (std::min)((t - 2.0f) / 0.5f, (9.0f - t) / 0.5f));
    }
    // B: 強い風（11〜16 秒）
    m_Grass.GetSettings().debugWindMul = (t >= 11.0f && t < 16.0f) ? 3.0f : 1.0f;
    // C: 正面 3m に止まった風源（18〜20 秒）
    if (t >= 18.0f && t < 20.0f)
        AddSource(m_Center + Vector3(0.6f, 0.0f, 3.0f), 3.0f, (std::min)((t - 18.0f) / 0.4f, 1.0f));

    if (m_Next < (int)(sizeof(kLooks) / sizeof(kLooks[0])) && t >= kLooks[m_Next].time)
    {
        Look(kLooks[m_Next].name);
        ++m_Next;
    }
    if (t >= 21.0f)
    {
        m_Grass.GetSettings().debugWindMul = 1.0f;
        AutoTestLog("grassfx done");
        m_Done = true;
    }
}

REGISTER_BATTLE_AUTOTEST("grassfx", AutoTestGrassFx)
