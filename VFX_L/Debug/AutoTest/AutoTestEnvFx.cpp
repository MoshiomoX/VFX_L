// ============================================================
// AutoTestEnvFx.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=envfx
// 特効の環境への干渉（2026-10-13、ユーザー：「全部の特効の環境への干渉を強く」）の見た目：
//   A 雑魚の足元：正面 8〜11m の半円から 10 体が歩いて来る。カメラ背後 8m / 俯角 20 度（見下ろすと倒れた草が分からない。低い角度で）。
//     `envfx look walk1 / walk2`
//   B 光線の帯：的 1 体（12m）、光線（ホーミング + アーク）。光線の出た 0.7 / 1.3 秒後 `envfx look beam1 / beam2`
//   C 爆発：的 1 体（8m）、メテオ（ファイアボール + ストーンショット）。カメラの揺れ（trauma）が上がった瞬間 = 爆発、
//     0.1 / 0.45 / 1.0 / 2.0 秒後 `envfx look blast1..4`（火 → 煙 → 倒れた草が残っているか）。done
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestEnvFx final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        // 踏み跡の読み戻し：プレイヤーから (dx, dz) の周り r m で強さ ≥ 0.3 の texel 数と最大値
        void Stats(const char* name, float dx, float dz, float r)
        {
            const Vector3 p = m_Registry.Get<TransformComponent>(m_Player).position;
            int n = 0; float mx = 0.0f;
            const bool ok = m_Grass.DebugTrampleStats(Vector2(p.x + dx, p.z + dz), r, 0.3f, n, mx);
            char line[128];
            snprintf(line, sizeof(line), "envfx trample %s ok %d count %d max %.2f", name, ok ? 1 : 0, n, mx);
            AutoTestLog(line);
        }
        void Reset();
        void SetCamera(float pitch, float dist, float yaw = 0.0f);
        float m_PhaseAt = 0.0f;
        float m_BeamAt = -1.0f;
        float m_BlastAt = -1.0f;
        int   m_Look = 0;
    };
}

void AutoTestEnvFx::SetCamera(float pitch, float dist, float yaw)
{
    auto& cam = m_Camera.Camera();
    cam.SetYaw(yaw);
    cam.SetPitch(pitch);
    cam.distance = dist;
    cam.avoidOcclusion = false;
    cam.SnapToTarget();
}

void AutoTestEnvFx::Reset()
{
    m_Swarm.KillAll();
    m_Swarm.ClearProjectiles();
    m_Swarm.ClearAreas();
    if (m_Registry.Has<BackpackComponent>(m_Player))
    {
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        ClearBackpackItems(bp);
        bp.dirty = true;
    }
    if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
}

void AutoTestEnvFx::Run()
{
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const float groundY = m_Swarm.GetAIParams().groundY;
    auto spawnAt = [&](float dx, float dz, float hp, float speed)
    {
        const float x = tf.position.x + dx, z = tf.position.z + dz;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), hp, speed, Swarm::kEnemyKindMob);
    };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<ManaComponent>(m_Player))
        {
            auto& mp = m_Registry.Get<ManaComponent>(m_Player);
            mp.max = mp.current = 1.0e6f;
        }
        if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            bp.frames.clear();
            BackpackLogic::RebuildFrameOccupancy(bp);
            for (int r = 0; r < BackpackComponent::GRID; ++r)
                for (int c = 0; c < BackpackComponent::GRID; ++c)
                    BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
        }
        Reset();
        // A: 歩いて来る雑魚（正面の半円 8〜11m）
        for (int k = 0; k < 10; ++k)
        {
            const float ang = DirectX::XMConvertToRadians(-65.0f + 130.0f * (float)k / 9.0f);
            const float r = 8.0f + 3.0f * (float)((k * 7) % 10) / 9.0f;
            spawnAt(std::sin(ang) * r, std::cos(ang) * r, 100000.0f, 3.5f);
        }
        SetCamera(20.0f, 8.0f);
        AutoTestLog("envfx start A (walking enemies)");
        m_PhaseAt = m_AutoTime;
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    const float t = m_AutoTime - m_PhaseAt;
    if (m_AutoStep == 1)
    {
        if (m_Look == 0 && t >= 1.2f) { AutoTestLog("envfx look walk1"); m_Look = 1; }
        if (m_Look == 1 && t >= 2.6f) { AutoTestLog("envfx look walk2"); Stats("walk2 player6m", 0.0f, 0.0f, 6.0f); Stats("walk2 ref", 12.0f, -12.0f, 3.0f); m_Look = 2; }
        if (m_Look == 2 && t >= 3.4f)
        {
            // B: 光線
            Reset();
            spawnAt(0.0f, 12.0f, 100000.0f, 0.0f);
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            BackpackLogic::Place(bp, ItemID::Beam, 6, 5, 0);
            BackpackLogic::Place(bp, ItemID::HomingBolt, 5, 4, 0);
            BackpackLogic::Place(bp, ItemID::ArcBolt, 7, 6, 0);
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
            SetCamera(18.0f, 9.0f, -90.0f);
            AutoTestLog("envfx start B (beam)");
            m_PhaseAt = m_AutoTime;
            m_Look = 0;
            m_AutoStep = 2;
        }
    }
    else if (m_AutoStep == 2)
    {
        if (m_BeamAt < 0.0f && m_WeaponSystem.GetActiveBeamCount() > 0)
        {
            m_BeamAt = m_AutoTime;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;   // 1 本だけ
        }
        if (m_BeamAt >= 0.0f)
        {
            const float bt = m_AutoTime - m_BeamAt;
            if (m_Look == 0 && bt >= 0.7f) { AutoTestLog("envfx look beam1"); m_Look = 1; }
            if (m_Look == 1 && bt >= 1.3f) { AutoTestLog("envfx look beam2"); Stats("beam2 online", 0.0f, 6.0f, 2.0f); Stats("beam2 side", 6.0f, 6.0f, 2.0f); m_Look = 2; }
            if (m_Look == 2 && bt >= 2.2f)
            {
                // C: 爆発（メテオ）
                Reset();
                spawnAt(0.0f, 8.0f, 100000.0f, 0.0f);
                auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
                BackpackLogic::Place(bp, ItemID::Meteor, 2, 2, 0);
                BackpackLogic::Place(bp, ItemID::Fireball, 0, 3, 0);
                BackpackLogic::Place(bp, ItemID::StoneShot, 3, 1, 0);
                bp.dirty = true;
                m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
                SetCamera(22.0f, 10.0f);
                AutoTestLog("envfx start C (meteor blast)");
                m_PhaseAt = m_AutoTime;
                m_Look = 0;
                m_AutoStep = 3;
            }
        }
        if (t >= 15.0f) { AutoTestLog("envfx done (beam timeout)"); m_AutoStep = 9; }
    }
    else if (m_AutoStep == 3)
    {
        if (m_BlastAt < 0.0f && m_Camera.Camera().GetTrauma() > 0.2f)
        {
            m_BlastAt = m_AutoTime;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;   // 爆発 1 回だけ
            AutoTestLog("envfx blast");
        }
        if (m_BlastAt >= 0.0f)
        {
            const float bt = m_AutoTime - m_BlastAt;
            if (m_Look == 0 && bt >= 0.10f) { AutoTestLog("envfx look blast1"); m_Look = 1; }
            if (m_Look == 1 && bt >= 0.45f) { AutoTestLog("envfx look blast2"); Stats("blast2 target5m", 0.0f, 8.0f, 5.0f); m_Look = 2; }
            if (m_Look == 2 && bt >= 1.00f) { AutoTestLog("envfx look blast3"); Stats("blast3 target5m", 0.0f, 8.0f, 5.0f); m_Look = 3; }
            if (m_Look == 3 && bt >= 2.00f) { AutoTestLog("envfx look blast4"); Stats("blast4 target5m", 0.0f, 8.0f, 5.0f); m_Look = 4; }
            if (m_Look == 4 && bt >= 2.5f) { AutoTestLog("envfx done"); m_AutoStep = 9; }
        }
        if (t >= 20.0f) { AutoTestLog("envfx done (blast timeout)"); m_AutoStep = 9; }
    }
}

REGISTER_BATTLE_AUTOTEST("envfx", AutoTestEnvFx)
