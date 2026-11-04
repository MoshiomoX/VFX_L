// ============================================================
// AutoTestMiasma.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=miasma（VFXL_STAGE=3 = 遺跡で）
// 遺跡の瘴気（World/MiasmaField、2026-11-03）：
//   1 秒   : 湧き停止・全消し・無敵・詠唱停止・HUD を隠す。正面 7m の柱の濃さ（湧いた量）
//   2.5 秒 : look idle（普段のカメラ）、look low（低い角度）
//   4 秒   : 竜巻くらいの風源が正面 7m を左から右へ 4 秒 → 風源の真ん中の柱がどこまで上がったか、look column1 / column2
//   9 秒   : 正面 7m で爆発の範囲（SWARM_AREA_SHAKE）→ 0.4 秒後 look blast、真ん中の柱
//   11 秒  : 左 5m でメテオの炎のレシピの範囲（熱 > 0）2 秒 → look fire、柱
//   14 秒  : プレイヤーの前で突風の風 → look gust
//   16〜22 秒: 晴れた所が戻るのを 2 秒毎に、22.5 秒 フレーム時間の内訳、done
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "World/MiasmaField.h"
#include "Debug/FrameProfiler.h"
#include "VFX_Editor/VFXId.h"

namespace
{
    class AutoTestMiasma final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Probe(const char* tag, const Vector3& p);
        void Look(const Vector3& p, float dist, float pitch);
        Vector3 Front(float side, float ahead) const { return Vector3(m_Base.x + side, m_Base.y, m_Base.z + ahead); }
        void Area(const Vector3& at, float radius, float time, uint32_t flags, uint32_t vfx);
        Vector3 m_Base;
        Vector3 m_LookAt;
    };
}

void AutoTestMiasma::Probe(const char* tag, const Vector3& p)
{
    float sum = 0.0f, top = 0.0f;
    float c[MiasmaField::kNY] = {};
    const bool ok = m_Miasma.DebugReadColumn(p, sum, top, c);
    char line[300];
    snprintf(line, sizeof(line), "miasma probe %s ok %d at (%.1f, %.1f) column %.3f top %.2f  h0 %.3f h0.9 %.3f h1.5 %.3f h3 %.3f h6 %.3f h9 %.3f",
        tag, ok ? 1 : 0, p.x, p.z, sum, top, c[0], c[3], c[5], c[10], c[20], c[30]);
    AutoTestLog(line);
}

void AutoTestMiasma::Look(const Vector3& p, float dist, float pitch)
{
    m_LookAt = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 0.9f, p.z);
    m_Camera.SetTargetOverride(&m_LookAt);
    auto& cam = m_Camera.Camera();
    cam.distance = dist;
    cam.SetPitch(pitch);
    cam.SnapToTarget();
}

void AutoTestMiasma::Area(const Vector3& at, float radius, float time, uint32_t flags, uint32_t vfx)
{
    Swarm::Area a;
    a.center = at;
    a.center.y = m_Grid.SampleHeight(at.x, at.z);
    a.radius = radius;
    a.halfHeight = 4.0f;
    a.damage = 0.0f;
    a.tickInterval = 1.0f;
    a.timeLeft = time;
    a.flags = flags;
    a.vfxType = vfx;
    m_Swarm.SpawnArea(a);
}

void AutoTestMiasma::Run()
{
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_GameUI.hideAll = true;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_Base = m_Registry.Get<TransformComponent>(m_Player).position;
        char line[160];
        snprintf(line, sizeof(line), "miasma start stage %d active %d", m_StageIndex, m_Miasma.Active() ? 1 : 0);
        AutoTestLog(line);
        Probe("front", Front(0.0f, 7.0f));
        Probe("player", m_Base);
        Look(Front(0.0f, 4.0f), 14.0f, 45.0f);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 1 && m_AutoTime >= 2.5f) { AutoTestLog("miasma look idle"); Look(Front(0.0f, 6.0f), 11.0f, 18.0f); m_AutoStep = 2; }
    if (m_AutoStep == 2 && m_AutoTime >= 3.2f) { AutoTestLog("miasma look low"); m_AutoStep = 3; }

    // 竜巻くらいの風源が正面 7m を左から右へ（砂の自動テストと同じ強さ）
    if (m_AutoStep >= 3 && m_AutoStep <= 6 && m_AutoTime >= 4.0f)
    {
        const float t = m_AutoTime - 4.0f;
        Vector3 p = Front(-6.0f + 3.0f * t, 7.0f);
        p.y = m_Grid.SampleHeight(p.x, p.z);
        Swarm::WindSource w = {};
        w.pos[0] = p.x; w.pos[1] = p.y; w.pos[2] = p.z;
        w.radius = 5.5f;
        w.swirl = 13.0f;
        w.updraft = 4.5f;
        w.inflow = 1.8f;
        w.fade = 1.0f;
        if (t < 4.0f) m_Swarm.AddFrameWindSource(w);
        if (m_AutoStep == 3) { Look(Front(0.0f, 7.0f), 16.0f, 22.0f); m_AutoStep = 4; }
        if (m_AutoStep == 4 && t >= 2.0f)
        {
            AutoTestLog("miasma look column1");
            Probe("vortex core", p);
            Probe("vortex 2m", p + Vector3(0.0f, 0.0f, 2.0f));
            Probe("far", Front(0.0f, 20.0f));
            m_AutoStep = 5;
        }
        if (m_AutoStep == 5 && t >= 3.2f) { AutoTestLog("miasma look column2"); Probe("vortex core later", p); m_AutoStep = 6; }
        if (m_AutoStep == 6 && t >= 4.5f) { Probe("after vortex", Front(2.0f, 7.0f)); m_AutoStep = 7; }
    }
    if (m_AutoStep == 7 && m_AutoTime >= 9.0f)
    {
        Probe("before blast", Front(0.0f, 9.0f));
        Area(Front(0.0f, 9.0f), 3.0f, 0.3f, Swarm::kAreaShake, 0u);
        Look(Front(0.0f, 9.0f), 14.0f, 40.0f);
        m_AutoStep = 8;
    }
    if (m_AutoStep == 8 && m_AutoTime >= 9.4f) { AutoTestLog("miasma look blast"); Probe("blast", Front(0.0f, 9.0f)); m_AutoStep = 9; }
    if (m_AutoStep == 9 && m_AutoTime >= 11.0f)
    {
        const uint32_t vfx = m_Swarm.GetVFXTable().IndexOf(VFXId::MeteorBlast);
        Probe("before fire", Front(-5.0f, 5.0f));
        Area(Front(-5.0f, 5.0f), 2.5f, 2.0f, 0u, vfx);
        Look(Front(-5.0f, 5.0f), 14.0f, 40.0f);
        char line[80];
        snprintf(line, sizeof(line), "miasma fire recipe %u", vfx);
        AutoTestLog(line);
        m_AutoStep = 10;
    }
    if (m_AutoStep == 10 && m_AutoTime >= 12.5f) { AutoTestLog("miasma look fire"); Probe("fire", Front(-5.0f, 5.0f)); m_AutoStep = 11; }
    if (m_AutoStep == 11 && m_AutoTime >= 14.0f)
    {
        SwarmSystem::WindBurst b;   // = CollisionTestScene::AddGustWindBurst（突風の魔法と同じ風）
        b.pos = Front(0.0f, 1.9f);
        b.radius = 3.4f;
        b.swirl = 11.0f;
        b.updraft = 3.5f;
        b.inflow = 1.2f;
        b.dir = Vector2(0.0f, 1.0f);
        b.push = 11.0f;
        b.life = 0.8f;
        m_Swarm.AddWindBurst(b);
        Look(Front(0.0f, 4.0f), 12.0f, 35.0f);
        m_AutoStep = 12;
    }
    if (m_AutoStep == 12 && m_AutoTime >= 14.4f) { AutoTestLog("miasma look gust"); Probe("gust", Front(0.0f, 3.0f)); m_AutoStep = 13; }
    if (m_AutoStep >= 13 && m_AutoStep <= 16 && m_AutoTime >= 16.0f + 2.0f * (m_AutoStep - 13))
    {
        char tag[40];
        snprintf(tag, sizeof(tag), "regrow fire t+%.0f", m_AutoTime - 11.0f);
        Probe(tag, Front(-5.0f, 5.0f));
        snprintf(tag, sizeof(tag), "regrow blast t+%.0f", m_AutoTime - 9.0f);
        Probe(tag, Front(0.0f, 9.0f));
        ++m_AutoStep;
    }
    if (m_AutoStep == 17 && m_AutoTime >= 22.5f)
    {
        Look(Front(0.0f, 4.0f), 14.0f, 45.0f);
        AutoTestLog(("  prof " + FrameProfiler::Get().Summary()).c_str());
        AutoTestLog("miasma look end");
        m_AutoStep = 18;
    }
    if (m_AutoStep == 18 && m_AutoTime >= 23.0f) { AutoTestLog("miasma done"); m_AutoStep = 19; }
}

REGISTER_BATTLE_AUTOTEST("miasma", AutoTestMiasma)
