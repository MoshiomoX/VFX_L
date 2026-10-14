// ============================================================
// AutoTestBeamTrack.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=beamtrack
// VFXL_BATTLE_AUTOTEST=beamtrack：魔導光線の標的が死んだら、向きに近い次の敵へ一定の速さで回るか
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBeamTrack final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 光線の標的の乗り換え（VFXL_BATTLE_AUTOTEST=beamtrack、2026-10-01）
// 1 秒: 湧き停止・全消し・無敵・魔力無限、バックパックは beam と同じ（光線 + 追尾弾 + 弧）。
//   A = 正面 10m・HP 60（最初の標的。光線で倒れる）、B = 右 50 度 11m・HP 10 万、C = 左 75 度 11m・HP 10 万。
//   光線が出たら詠唱を止める（1 本だけ見る）。期待: A を捕まえる → A が死ぬ → 角度の近い B へ
//   beamTurnRate（90 度/秒）以下の速さで回る（C ではない）。
// 光線が出ている間 0.05 秒毎に `beamtrack t yaw hasTarget target(x,z) alive`（yaw = プレイヤーから見た光線の向き、度、+ = 右）。
// 出てから 0.7 / 1.2 / 1.7 秒に `beamtrack look <n>`（真上寄りから撮る）。光線が消えて 0.5 秒で done
// ============================================================
void AutoTestBeamTrack::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (!m_Registry.Has<BackpackComponent>(m_Player) || !m_Registry.Has<WandComponent>(m_Player)) return;
    auto& cam = m_Camera.Camera();
    static float s_NextLog = 0.0f, s_BeamStart = -1.0f, s_BeamEnd = -1.0f;
    static int s_Looks = 0;
    static Vector3 s_Origin;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        s_Origin = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        auto at = [&](float deg, float dist)
            {
                const float r = DirectX::XMConvertToRadians(deg);
                return Vector3(s_Origin.x + std::sin(r) * dist, gy, s_Origin.z + std::cos(r) * dist);
            };
        m_Swarm.SpawnEnemy(at(0.0f, 10.0f), 60.0f, 0.0f);       // A
        m_Swarm.SpawnEnemy(at(50.0f, 11.0f), 100000.0f, 0.0f);  // B
        m_Swarm.SpawnEnemy(at(-75.0f, 11.0f), 100000.0f, 0.0f); // C

        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        const int lo = BackpackComponent::GRID / 2 - 1;
        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, ItemID::Beam, lo, lo + 1, 0);
        BackpackLogic::Place(bp, ItemID::HomingBolt, lo + 1, lo + 2, 0);
        BackpackLogic::Place(bp, ItemID::ArcBolt, lo + 1, lo, 0);
        bp.dirty = true;
        m_Registry.Get<WandComponent>(m_Player).castingPaused = false;

        cam.SetYaw(0.0f);   // 後ろから +Z を見下ろす
        cam.distance = 18.0f;
        cam.SetPitch(60.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("beamtrack start: A 0deg 10m hp60, B +50deg 11m, C -75deg 11m, turn rate 90");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1)
    {
        Vector3 dir, tgt;
        bool hasT = false;
        const bool active = m_WeaponSystem.GetBeamDebug(0, dir, hasT, tgt);
        if (active && s_BeamStart < 0.0f)
        {
            s_BeamStart = m_AutoTime;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;   // この 1 本だけ見る
        }
        if (!active && s_BeamStart > 0.0f && s_BeamEnd < 0.0f) s_BeamEnd = m_AutoTime;

        if (active && m_AutoTime >= s_NextLog)
        {
            s_NextLog = m_AutoTime + 0.05f;
            const float yaw = DirectX::XMConvertToDegrees(std::atan2(dir.x, dir.z));
            char line[160];
            snprintf(line, sizeof(line), "beamtrack t %.2f yaw %.1f hasTarget %d target (%.1f,%.1f) alive %u",
                m_AutoTime - s_BeamStart, yaw, hasT ? 1 : 0, tgt.x - s_Origin.x, tgt.z - s_Origin.z,
                m_Swarm.GetCounters().aliveEnemies);
            AutoTestLog(line);
        }
        if (s_BeamStart > 0.0f)
        {
            const float t = m_AutoTime - s_BeamStart;
            if ((s_Looks == 0 && t >= 0.7f) || (s_Looks == 1 && t >= 1.2f) || (s_Looks == 2 && t >= 1.7f))
            {
                char line[32];
                snprintf(line, sizeof(line), "beamtrack look %d", s_Looks++);
                AutoTestLog(line);
            }
        }
        if ((s_BeamEnd > 0.0f && m_AutoTime >= s_BeamEnd + 0.5f) || m_AutoTime >= 20.0f)
        {
            AutoTestLog("beamtrack done");
            m_AutoStep = 2;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("beamtrack", AutoTestBeamTrack)
