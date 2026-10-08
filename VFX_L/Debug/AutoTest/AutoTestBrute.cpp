// ============================================================
// AutoTestBrute.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=brute
// 重装兵（kEnemyKindBrute。2026-10-07 夜）：雑魚と並べて、見た目（オークのテクスチャ・体格）・HP・速さを確かめる
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBrute final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// 1 秒: 湧き停止・全消し・詠唱停止。プレイヤーの正面 +Z 10〜12m に重装兵 6 体と雑魚 6 体を交互に並べる。カメラは背中から 12m / 28°
// 2.5 秒: `brute look near`（近づいてくる所）。2 秒・3 秒にリードバックで種類毎の数・平均 HP・平均の水平速度
// 4 秒: 詠唱を戻す。7 秒: 残りの数、`brute look fight`。8 秒: done
// ============================================================
void AutoTestBrute::Run()
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
        const float groundY = m_Swarm.GetAIParams().groundY;
        for (int i = 0; i < 12; ++i)
        {
            const bool brute = (i % 2) == 0;
            const uint32_t kind = brute ? Swarm::kEnemyKindBrute : Swarm::kEnemyKindMob;
            float hp = 15.0f, speed = 3.5f;
            m_Mobs.KindStats(kind, hp, speed);
            const float x = tf.position.x + ((float)i - 5.5f) * 1.8f;
            const float z = tf.position.z + 10.0f + (brute ? 2.0f : 0.0f);
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), hp, speed, kind);
        }
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(28.0f);
        cam.distance = 12.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("brute start");
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    auto report = [&](const char* tag)
    {
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        std::vector<Swarm::EnemyExtra> ex;
        if (!m_Swarm.DebugReadEnemies(en, st, &ex)) return;
        int n[2] = {};
        float hp[2] = {}, spd[2] = {};
        for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
        {
            if (st[i] == Swarm::kStateDead) continue;
            const int k = (ex[i].kind == Swarm::kEnemyKindBrute) ? 0 : (ex[i].kind == Swarm::kEnemyKindMob) ? 1 : -1;
            if (k < 0) continue;
            ++n[k];
            hp[k] += Swarm::HpFromFixed(en[i].hp);
            spd[k] += std::sqrt(en[i].velocity.x * en[i].velocity.x + en[i].velocity.z * en[i].velocity.z);
        }
        snprintf(line, sizeof(line), "brute %s t %.1f brutes %d hp %.1f speed %.2f | mobs %d hp %.1f speed %.2f", tag, m_AutoTime,
            n[0], n[0] ? hp[0] / n[0] : 0.0f, n[0] ? spd[0] / n[0] : 0.0f, n[1], n[1] ? hp[1] / n[1] : 0.0f, n[1] ? spd[1] / n[1] : 0.0f);
        AutoTestLog(line);
    };

    if (m_AutoStep == 1 && m_AutoTime >= 2.0f) { report("read"); m_AutoStep = 2; }
    if (m_AutoStep == 2 && m_AutoTime >= 2.5f) { AutoTestLog("brute look near"); m_AutoStep = 3; }
    if (m_AutoStep == 3 && m_AutoTime >= 3.0f) { report("read"); m_AutoStep = 4; }
    if (m_AutoStep == 4 && m_AutoTime >= 4.0f)
    {
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        m_AutoStep = 5;
    }
    if (m_AutoStep == 5 && m_AutoTime >= 7.0f) { report("fight"); AutoTestLog("brute look fight"); m_AutoStep = 6; }
    if (m_AutoStep == 6 && m_AutoTime >= 8.0f) { AutoTestLog("brute done"); m_AutoStep = 7; }
}

REGISTER_BATTLE_AUTOTEST("brute", AutoTestBrute)
