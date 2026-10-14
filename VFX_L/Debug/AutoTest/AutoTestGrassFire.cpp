// ============================================================
// AutoTestGrassFire.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=grassfire
// 草の延焼（GrassFireField。2026-10-12）：
//   A) 1 秒: 湧き停止・全消し・無敵・詠唱停止。正面 +Z 7m を半径 2m で点火（GrassFireField::Ignite）。
//      カメラは背中から 11m / 30°。以後 0.5 秒ごとに温度 / 燃料を読み戻して
//      `grassfire t burning charred`（燃えているマス / 燃料が半分以下のマス）。2 / 4 / 7 秒に `grassfire look <n>`
//   B) 8 秒: 正面 9m に動かない雑魚 3 体、バックパックに火球だけ。火球の命中（FireballHit、heat 0.7）で草に火が付くか。
//      11 / 14 秒に読み戻し + `grassfire look hit1 / hit2`
//   C) 15 秒: 雨を強制（WeatherSystem::TriggerNow、timeOverride は昼のまま）。18 秒に読み戻し（burning が減っているはず）+ `look rain`。19 秒 done
// 期待：A の 2 秒で burning > 0、4 秒で charred が増え続ける（風下へ伸びる）、B で命中後 burning が再び > 0、C で burning が減る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "World/GrassFireField.h"

namespace
{
    class AutoTestGrassFire final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        float m_NextRead = 1.5f;
    };
}

void AutoTestGrassFire::Run()
{
    char line[200];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<ManaComponent>(m_Player))
        {
            auto& mp = m_Registry.Get<ManaComponent>(m_Player);
            mp.max = mp.current = 1.0e6f;
        }
        m_PlayerControlSystem.testInput = true;   // 入力 0（立ったまま）
        m_PlayerControlSystem.testMove = Vector2::Zero;
        m_Weather.eventsEnabled = false;

        m_GrassFire.GetSettings().enabled = true;   // 既定は切（ユーザー判断待ち）
        const Vector3 at(tf.position.x, 0.0f, tf.position.z + 7.0f);
        m_GrassFire.Ignite(at, 2.0f);

        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(30.0f);
        cam.distance = 11.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "grassfire start active %d wind yaw %.0f speed %.2f", m_GrassFire.Active() ? 1 : 0,
            m_Weather.Wind().YawDeg(), m_Weather.Wind().speed * m_Weather.Wind().strength);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    if (m_AutoTime >= m_NextRead)
    {
        m_NextRead += 0.5f;
        uint32_t burning = 0, charred = 0;
        if (m_GrassFire.DebugReadStats(burning, charred))
        {
            snprintf(line, sizeof(line), "grassfire t %.1f burning %u charred %u", m_AutoTime, burning, charred);
            AutoTestLog(line);
        }
    }

    if (m_AutoStep == 1 && m_AutoTime >= 2.0f) { AutoTestLog("grassfire look 0"); m_AutoStep = 2; }
    if (m_AutoStep == 2 && m_AutoTime >= 4.0f) { AutoTestLog("grassfire look 1"); m_AutoStep = 3; }
    if (m_AutoStep == 3 && m_AutoTime >= 7.0f) { AutoTestLog("grassfire look 2"); m_AutoStep = 4; }
    if (m_AutoStep == 4 && m_AutoTime >= 8.0f)
    {
        // B) 火球の命中で火が付くか
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        const float groundY = m_Swarm.GetAIParams().groundY;
        for (int k = 0; k < 3; ++k)
        {
            const float x = tf.position.x + (float)(k - 1) * 2.0f, z = tf.position.z + 9.0f;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 100000.0f, 0.0f);
        }
        AutoTestLog("grassfire fireball on");
        m_AutoStep = 5;
    }
    if (m_AutoStep == 5 && m_AutoTime >= 11.0f) { AutoTestLog("grassfire look hit1"); m_AutoStep = 6; }
    if (m_AutoStep == 6 && m_AutoTime >= 14.0f) { AutoTestLog("grassfire look hit2"); m_AutoStep = 7; }
    if (m_AutoStep == 7 && m_AutoTime >= 15.0f)
    {
        // C) 雨で消えるか（火球は止める）
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_Weather.fadeIn = 1.0f;
        m_Weather.TriggerNow(60.0f);
        AutoTestLog("grassfire rain on");
        m_AutoStep = 8;
    }
    if (m_AutoStep == 8 && m_AutoTime >= 18.0f)
    {
        snprintf(line, sizeof(line), "grassfire look rain intensity %.2f", m_Weather.Intensity());
        AutoTestLog(line);
        m_AutoStep = 9;
    }
    if (m_AutoStep == 9 && m_AutoTime >= 19.0f) { AutoTestLog("grassfire done"); m_AutoStep = 10; }
}

REGISTER_BATTLE_AUTOTEST("grassfire", AutoTestGrassFire)
