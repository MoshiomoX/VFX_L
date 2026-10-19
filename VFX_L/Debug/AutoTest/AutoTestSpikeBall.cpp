// ============================================================
// AutoTestSpikeBall.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=spikeball
// トゲ玉（kEnemyKindSpikeBall。2026-10-17）：正面に 1 体、プレイヤーは立ったまま。転がって来てぶつかり、
// プレイヤーが弾き飛ばされる距離・高さを測る。その後詠唱を戻して倒す
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Component/RigidbodyComponent.h"

namespace
{
    class AutoTestSpikeBall final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// 1 秒: 湧き停止・全消し・詠唱停止・HP 10 万。正面 +Z 7m にトゲ玉 1 体（HP は難度どおり）。カメラは右側 9m / 16°
// 1.3 秒: `spikeball look roll`。初めて HP が減った瞬間から 0.1 秒毎に `spikeball t dist height`（始点からの水平距離・足の高さ）、
//   0.15 秒後 `spikeball look hit`。1.2 秒後 `spikeball result maxDist maxHeight hits`。
// 5 秒: 詠唱を戻す。9 秒: 残りの数 `spikeball fight alive`、`spikeball look fight`。10 秒: done
// ============================================================
void AutoTestSpikeBall::Run(float dt)
{
    char line[200];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    static Vector3 s_Start;
    static float s_HitAt = -1.0f, s_NextLog = 0.0f, s_MaxDist = 0.0f, s_MaxH = 0.0f, s_LastHp = 0.0f;
    static int s_Hits = 0;
    static bool s_LookHit = false;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        if (m_Registry.Has<HealthComponent>(m_Player))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.max = hp.current = 100000.0f;
            s_LastHp = hp.current;
        }
        const float groundY = m_Swarm.GetAIParams().groundY;
        float hp = 15.0f, speed = 3.5f;
        m_Mobs.KindStats(Swarm::kEnemyKindSpikeBall, hp, speed);
        const float x = tf.position.x, z = tf.position.z + 7.0f;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), hp, speed, Swarm::kEnemyKindSpikeBall);
        s_Start = tf.position;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(-90.0f);
        cam.SetPitch(16.0f);
        cam.distance = 9.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "spikeball start hp %.0f speed %.2f", hp, speed);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    if (m_AutoStep == 1 && m_AutoTime >= 1.3f) { AutoTestLog("spikeball look roll"); m_AutoStep = 2; }

    // 被弾を見張る（HP が減った = ぶつかった）
    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        if (hp.current < s_LastHp - 0.5f)
        {
            ++s_Hits;
            if (s_HitAt < 0.0f) { s_HitAt = m_AutoTime; s_NextLog = m_AutoTime; s_Start = tf.position; }
        }
        s_LastHp = hp.current;
    }
    if (s_HitAt >= 0.0f && m_AutoStep < 4)
    {
        const float dx = tf.position.x - s_Start.x, dz = tf.position.z - s_Start.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        const float h = tf.position.y - s_Start.y;
        s_MaxDist = (std::max)(s_MaxDist, dist);
        s_MaxH = (std::max)(s_MaxH, h);
        if (m_AutoTime >= s_NextLog)
        {
            snprintf(line, sizeof(line), "spikeball t %.2f dist %.2f height %.2f", m_AutoTime - s_HitAt, dist, h);
            AutoTestLog(line);
            s_NextLog += 0.1f;
        }
        if (!s_LookHit && m_AutoTime >= s_HitAt + 0.15f) { AutoTestLog("spikeball look hit"); s_LookHit = true; }
        if (m_AutoTime >= s_HitAt + 1.2f && m_AutoStep < 3)
        {
            snprintf(line, sizeof(line), "spikeball result maxDist %.2f maxHeight %.2f hits %d", s_MaxDist, s_MaxH, s_Hits);
            AutoTestLog(line);
            m_AutoStep = 3;
        }
    }
    if (m_AutoStep <= 3 && m_AutoTime >= 5.0f)
    {
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        m_AutoStep = 4;
    }
    if (m_AutoStep == 4 && m_AutoTime >= 9.0f)
    {
        snprintf(line, sizeof(line), "spikeball fight alive %u hits %d", m_Swarm.GetCounters().aliveEnemies, s_Hits);
        AutoTestLog(line);
        AutoTestLog("spikeball look fight");
        m_AutoStep = 5;
    }
    if (m_AutoStep == 5 && m_AutoTime >= 10.0f) { AutoTestLog("spikeball done"); m_AutoStep = 6; }
    (void)dt;
}

REGISTER_BATTLE_AUTOTEST("spikeball", AutoTestSpikeBall)
