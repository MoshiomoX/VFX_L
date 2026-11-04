// ============================================================
// AutoTestSmartCam.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=smartcam
// 賢いカメラ（2026-11-02、BattleCamera::UpdateSmart）：
//   1 秒: 湧き停止・全消し・無敵・詠唱停止
//   段 0〜3（各 6 秒）: プレイヤーの周り 3〜9m に動かない雑魚を 0 / 20 / 60 / 120 体。0.5 秒毎に
//     `smartcam <段> t near want now dist`（near = GPU が数えた 10m 以内、want = 欲しい足し分、now = ばねの今、dist = カメラの距離）、
//     段の終わりに `smartcam look crowd<n>`
//   段 4: 全消し → 止まった Boss を呼ぶ（RequestBoss(false)、周り 12〜16m）。8 秒、同じ行 + frame（注視点のずらし）、
//     `smartcam look boss`
//   段 5: 設定「カメラの自動調整」をオフ → 4 秒後 `smartcam look off`（距離が 14m へ戻る）。done
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Core/GameSettings.h"

namespace
{
    class AutoTestSmartCam final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Crowd(int count);
        void Log(const char* tag);
        float m_NextLog = 0.0f;
        float m_PhaseStart = 0.0f;
        bool  m_PrevSmart = true;
        bool  m_Looked = false;
    };
}

void AutoTestSmartCam::Crowd(int count)
{
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const float groundY = m_Swarm.GetAIParams().groundY;
    m_Swarm.KillAll();
    for (int k = 0; k < count; ++k)
    {
        const float ang = 2.39996f * (float)k;              // 黄金角で散らす
        const float r = 3.0f + 6.0f * std::fmod(0.6180339f * (float)k, 1.0f);
        const float x = tf.position.x + std::sin(ang) * r, z = tf.position.z + std::cos(ang) * r;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 100000.0f, 0.0f, Swarm::kEnemyKindMob);
    }
}

void AutoTestSmartCam::Log(const char* tag)
{
    const FollowCamera& c = m_Camera.Camera();
    char line[240];
    snprintf(line, sizeof(line), "smartcam %s t %.2f near %u now %.2f dist %.2f frame %.2f %.2f boss %d",
        tag, m_AutoTime - m_PhaseStart, m_Swarm.GetNearEnemyCount(), c.GetSituationDistance(), c.GetCurrentDistance(),
        c.GetFrameOffset().x, c.GetFrameOffset().z, m_Stage.IsBossAlive() ? 1 : 0);
    AutoTestLog(line);
}

void AutoTestSmartCam::Run()
{
    static const int kCounts[4] = { 0, 20, 60, 120 };
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_PrevSmart = GameSettings::Get().smartCamera;
        GameSettings::Get().smartCamera = true;
        Crowd(kCounts[0]);
        m_PhaseStart = m_AutoTime;
        m_NextLog = m_AutoTime;
        m_AutoStep = 1;
    }
    // 段 1..4 = 雑魚の 4 段（m_AutoStep - 1 が添字）
    if (m_AutoStep >= 1 && m_AutoStep <= 4)
    {
        const int ph = m_AutoStep - 1;
        char tag[16];
        snprintf(tag, sizeof(tag), "crowd%d", kCounts[ph]);
        if (m_AutoTime >= m_NextLog) { Log(tag); m_NextLog += 0.5f; }
        // 撮影は入れ替えの前のフレームで（同じフレームで全消しすると写らない）
        if (!m_Looked && m_AutoTime - m_PhaseStart >= 5.5f)
        {
            char look[48];
            snprintf(look, sizeof(look), "smartcam look crowd%d", kCounts[ph]);
            AutoTestLog(look);
            m_Looked = true;
        }
        if (m_AutoTime - m_PhaseStart >= 6.0f)
        {
            m_Looked = false;
            m_PhaseStart = m_AutoTime;
            m_NextLog = m_AutoTime;
            if (ph < 3) Crowd(kCounts[ph + 1]);
            else
            {
                m_Swarm.KillAll();
                const bool ok = m_Stage.RequestBoss(false);
                AutoTestLog(ok ? "smartcam boss requested" : "smartcam boss request FAILED");
            }
            ++m_AutoStep;
        }
    }
    if (m_AutoStep == 5)
    {
        if (m_AutoTime >= m_NextLog) { Log("boss"); m_NextLog += 0.5f; }
        if (m_AutoTime - m_PhaseStart >= 8.0f)
        {
            AutoTestLog("smartcam look boss");
            GameSettings::Get().smartCamera = false;
            m_PhaseStart = m_AutoTime;
            m_NextLog = m_AutoTime;
            m_AutoStep = 6;
        }
    }
    if (m_AutoStep == 6)
    {
        if (m_AutoTime >= m_NextLog) { Log("off"); m_NextLog += 0.5f; }
        if (m_AutoTime - m_PhaseStart >= 4.0f)
        {
            AutoTestLog("smartcam look off");
            GameSettings::Get().smartCamera = m_PrevSmart;   // ファイルには書かない
            m_AutoStep = 7;
        }
    }
    if (m_AutoStep == 7 && m_AutoTime - m_PhaseStart >= 4.5f) { AutoTestLog("smartcam done"); m_AutoStep = 8; }
}

REGISTER_BATTLE_AUTOTEST("smartcam", AutoTestSmartCam)
