// ============================================================
// AutoTestDebris.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=debris
// 地面の落ち葉（World/GroundDebris、2026-10-13）の見た目：静止 → 突風で舞う → 爆発で散る → 強風で転がる。
//   1 秒: 湧き停止・全消し・無敵・MP 無限・入力 0。バックパックに突風だけ、正面 +Z 5m に動かない雑魚 3 体（HP 10 万）。
//   カメラは背後 7m / 俯角 24 度（足元の地面が見える）。落ち葉は密度 1、大きさ 1.3 倍。
//   1.5 秒 `debris look rest`（静止）。最初の発動の 0.12 / 0.3 / 0.6 / 1.2 秒後に `debris look blow1..4`。
//   4.5 秒: 正面 4m に爆発の範囲（kAreaShake、半径 3m、0.4 秒）→ 0.15 / 0.5 秒後に `debris look blast1 / 2`。
//   6.5 秒: 舞い上がる風速を 0.5 m/s にして普段の風でも転がる状態に → 1 秒後 `debris look wind`。8 秒 done
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestDebris final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        float m_FirstCast = -1.0f;
        float m_BlastAt = -1.0f;
        int   m_Look = 0;
    };
}

void AutoTestDebris::Run()
{
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<ManaComponent>(m_Player))
        {
            auto& mp = m_Registry.Get<ManaComponent>(m_Player);
            mp.max = mp.current = 1.0e6f;
        }
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Gust, lo + 1, lo, 0);
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;   // 標的のリードバックを待つ
        }
        const float groundY = m_Swarm.GetAIParams().groundY;
        for (int k = -1; k <= 1; ++k)
        {
            const float x = tf.position.x + 1.2f * (float)k, z = tf.position.z + 5.0f;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 100000.0f, 0.0f, Swarm::kEnemyKindMob);
        }
        auto& s = m_Debris.GetSettings();
        s.density = 1.0f;
        s.sizeMul = 1.3f;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(24.0f);
        cam.distance = 7.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("debris start");
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    if (m_Look == 0 && m_AutoTime >= 1.5f) { AutoTestLog("debris look rest"); m_Look = 1; }
    if (m_Look == 1 && m_AutoTime >= 1.7f && m_Registry.Has<WandComponent>(m_Player))
    {
        m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        m_Look = 2;
    }
    if (m_FirstCast < 0.0f && !m_WeaponSystem.GetGustFx().empty())
    {
        m_FirstCast = m_AutoTime;
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;   // 1 発だけ
    }
    if (m_FirstCast >= 0.0f)
    {
        const float t = m_AutoTime - m_FirstCast;
        if (m_Look == 2 && t >= 0.12f) { AutoTestLog("debris look blow1"); m_Look = 3; }
        if (m_Look == 3 && t >= 0.30f) { AutoTestLog("debris look blow2"); m_Look = 4; }
        if (m_Look == 4 && t >= 0.60f) { AutoTestLog("debris look blow3"); m_Look = 5; }
        if (m_Look == 5 && t >= 1.20f) { AutoTestLog("debris look blow4"); m_Look = 6; }
    }

    if (m_Look == 6 && m_AutoTime >= 4.5f)
    {
        Swarm::Area blast;
        blast.center = Vector3(tf.position.x, m_Grid.SampleHeight(tf.position.x, tf.position.z + 4.0f), tf.position.z + 4.0f);
        blast.radius = 3.0f;
        blast.damage = 0.0f;
        blast.tickInterval = 1.0e9f;
        blast.timeLeft = 0.4f;
        blast.flags = Swarm::kAreaShake;
        m_Swarm.SpawnArea(blast);
        m_BlastAt = m_AutoTime;
        AutoTestLog("debris blast spawned");
        m_Look = 7;
    }
    if (m_BlastAt >= 0.0f)
    {
        const float t = m_AutoTime - m_BlastAt;
        if (m_Look == 7 && t >= 0.15f) { AutoTestLog("debris look blast1"); m_Look = 8; }
        if (m_Look == 8 && t >= 0.50f) { AutoTestLog("debris look blast2"); m_Look = 9; }
    }

    if (m_Look == 9 && m_AutoTime >= 6.5f)
    {
        m_Debris.GetSettings().liftWind = 0.5f;   // 普段の風（約 1.2〜2 m/s）でも舞う
        m_Look = 10;
    }
    if (m_Look == 10 && m_AutoTime >= 7.5f) { AutoTestLog("debris look wind"); m_Look = 11; }
    if (m_Look == 11 && m_AutoTime >= 8.0f) { AutoTestLog("debris done"); m_Look = 12; }
}

REGISTER_BATTLE_AUTOTEST("debris", AutoTestDebris)
