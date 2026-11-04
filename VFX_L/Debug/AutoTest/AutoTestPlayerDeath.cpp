// ============================================================
// AutoTestPlayerDeath.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=playerdeath
// プレイヤーの死（2026-11-01。ユーザー：死んだ後も地面を這って動ける + 死の演出）：
//   1 秒: 湧き停止・全消し・無敵。入力は自動テストのもの。周り 4〜7m に歩いて来る雑魚 16 体（HP 10 万）
//   1.2 秒: 走らせて 2.0 秒まで（生きている間は動く = 比較用）
//   2.0 秒: HP 0。以後ずっと移動 + ジャンプ + 滑り + 魔力解放を入れ続ける
//   死の後は 0.25 秒毎（gameplay の秒 = スローの間は実時間で 1 秒毎）に
//   `playerdeath t pos moved rise surge mobs avgDist kills` = 体が動いていない事、敵が押し出される事、撃破が増えない事
// 撮影と `playerdeath done` はシーンが実時間で書く（CollisionTestScenePlayerDeath.cpp。この後リザルトへ移る）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestPlayerDeath final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        Vector3 m_DeadPos = Vector3::Zero;
        Vector3 m_AlivePos = Vector3::Zero;
        float m_NextLog = 0.0f;
        float m_MaxDist = 0.0f, m_MaxY = 0.0f;
        uint32_t m_KillsAtDeath = 0;
    };
}

void AutoTestPlayerDeath::Run()
{
    auto& pcs = m_PlayerControlSystem;
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& hp = m_Registry.Get<HealthComponent>(m_Player);
    char line[220];

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        hp.invincible = true;
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        pcs.testInput = true;
        pcs.testMove = Vector2::Zero;
        const float groundY = m_Swarm.GetAIParams().groundY;
        for (int k = 0; k < 16; ++k)
        {
            const float a = 6.2831853f * (float)k / 16.0f, r = 4.0f + 3.0f * (float)(k % 4) / 3.0f;
            const float x = tf.position.x + std::sin(a) * r, z = tf.position.z + std::cos(a) * r;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 100000.0f, 3.0f, Swarm::kEnemyKindMob);
        }
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 1.2f)
    {
        m_AlivePos = tf.position;
        pcs.testMove = Vector2(1.0f, 0.0f);
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 2.0f)
    {
        snprintf(line, sizeof(line), "playerdeath alive moved %.2f m in 0.8 s", (tf.position - m_AlivePos).Length());
        AutoTestLog(line);
        hp.invincible = false;
        hp.current = 0.0f;
        m_KillsAtDeath = m_Swarm.GetCounters().killCount;
        m_AutoStep = 3;
        m_NextLog = m_AutoTime;
    }
    else if (m_AutoStep == 3)
    {
        // PlayerStateSystem が Dead にしてから（次のフレーム）の位置を基準にする
        if (m_DeadPos == Vector3::Zero && m_Registry.Get<PlayerStateComponent>(m_Player).IsDead())
            m_DeadPos = tf.position;

        pcs.testMove = Vector2(0.7f, 0.7f);
        pcs.testJump = true;
        pcs.testSlide = true;
        pcs.testSurge = true;

        if (m_DeadPos != Vector3::Zero)
        {
            const float dx = tf.position.x - m_DeadPos.x, dz = tf.position.z - m_DeadPos.z;
            m_MaxDist = (std::max)(m_MaxDist, std::sqrt(dx * dx + dz * dz));
            m_MaxY = (std::max)(m_MaxY, tf.position.y - m_DeadPos.y);
        }
        if (m_AutoTime >= m_NextLog)
        {
            m_NextLog += 0.25f;
            float sum = 0.0f; int mobs = 0;
            std::vector<Swarm::Enemy> en; std::vector<uint32_t> st;
            if (m_Swarm.DebugReadEnemies(en, st))
                for (size_t i = 0; i < en.size(); ++i)
                {
                    if (st[i] == Swarm::kStateDead) continue;
                    const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
                    sum += std::sqrt(dx * dx + dz * dz);
                    ++mobs;
                }
            snprintf(line, sizeof(line), "playerdeath t dead %d moved %.3f rise %.3f surge %.2f mobs %d avgDist %.2f kills +%u",
                m_Registry.Get<PlayerStateComponent>(m_Player).IsDead() ? 1 : 0, m_MaxDist, m_MaxY,
                m_Registry.Get<ManaComponent>(m_Player).surgeTime, mobs, mobs ? sum / (float)mobs : 0.0f,
                m_Swarm.GetCounters().killCount - m_KillsAtDeath);
            AutoTestLog(line);
        }
    }
}

REGISTER_BATTLE_AUTOTEST("playerdeath", AutoTestPlayerDeath)
