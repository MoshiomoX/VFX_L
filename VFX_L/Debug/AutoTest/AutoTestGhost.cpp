// ============================================================
// AutoTestGhost.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=ghost
// 最終ウェーブの幽霊（白 kEnemyKindGhost / 黒 kEnemyKindDarkGhost。2026-10-18 に強化 + 黒幽霊）：
// A 白幽霊 20 体、B 黒幽霊 5 体。プレイヤーは立ったまま撃つ（HP 10 万）。数・壁抜け・近づく速さ・
// 当たった回数・ノックバックの距離・倒せた数を記録し撮る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestGhost final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// 1 秒: 湧き停止・全消し・HP 10 万（無敵ではない：当たった回数を数える）・詠唱はそのまま（開始の魔法で撃つ）、
//   カメラ背後 14m / 30°。白幽霊 20 体（プレイヤーの周りの環 37.5〜52.5m。壁を素通りするので歩けるマスかは見ない）
// 0.5 秒毎に `ghost A t alive inWall nearest hpAvg hits lost`（hpAvg = 生きている幽霊の HP の平均：撃たれても落ちないか）。
//   当たる毎に 0.4 秒後 `ghost knock A dist lift`（押された距離 / 浮いた高さ）。5 秒 `look A0`、初めて当たった 0.1 秒後 `look A1`
// 13 秒: `ghost A result alive killed hits lost knockMax` → 全消し、黒幽霊 5 体 → 同じ記録（B）。初めて 10m 内に来たら `look B0`
// 26 秒: `ghost B result ...` + done
// ============================================================
void AutoTestGhost::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    static float s_NextLog = 0.0f, s_LastHp = 0.0f, s_BaseY = 0.0f, s_KnockMax = 0.0f, s_KnockAt = -1.0f, s_FirstHitAt = -1.0f;
    static int s_Hits = 0;
    static float s_Lost = 0.0f;
    static uint32_t s_KillsStart = 0;
    static Vector3 s_HitPos;
    static bool s_Look0 = false, s_Look1 = false;
    char line[200];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const char phase = (m_AutoStep <= 1) ? 'A' : 'B';

    auto beginPhase = [&](int whiteN, int darkN)
    {
        m_Swarm.KillAll();
        if (whiteN > 0) m_Mobs.QueueDebugGhosts(whiteN);
        if (darkN > 0) m_Mobs.QueueDebugDarkGhosts(darkN);
        s_Hits = 0; s_Lost = 0.0f; s_KnockMax = 0.0f; s_KnockAt = -1.0f; s_FirstHitAt = -1.0f;
        s_Look0 = s_Look1 = false;
        s_KillsStart = m_Swarm.GetCounters().killCount;
        s_NextLog = m_AutoTime + 0.5f;
        float hp = 15.0f, speed = 3.5f;
        m_Mobs.KindStats(whiteN > 0 ? Swarm::kEnemyKindGhost : Swarm::kEnemyKindDarkGhost, hp, speed);
        snprintf(line, sizeof(line), "ghost %c start white %d dark %d hp %.0f speed %.2f", (m_AutoStep == 0) ? 'A' : 'B', whiteN, darkN, hp, speed);
        AutoTestLog(line);
    };
    auto result = [&](char ph)
    {
        snprintf(line, sizeof(line), "ghost %c result alive %u killed %u hits %d lost %.0f knockMax %.2f",
            ph, m_Swarm.GetCounters().aliveEnemies, m_Swarm.GetCounters().killCount - s_KillsStart, s_Hits, s_Lost, s_KnockMax);
        AutoTestLog(line);
    };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<HealthComponent>(m_Player))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.max = hp.current = 100000.0f;
            s_LastHp = hp.current;
        }
        s_BaseY = tf.position.y;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(30.0f);
        cam.distance = 14.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        beginPhase(20, 0);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0 || m_AutoStep >= 3) return;

    // ---- 当たった回数・ノックバック ----
    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        if (hp.current < s_LastHp - 0.5f)
        {
            s_Lost += s_LastHp - hp.current;
            ++s_Hits;
            if (s_FirstHitAt < 0.0f) s_FirstHitAt = m_AutoTime;
            if (s_KnockAt < 0.0f) { s_KnockAt = m_AutoTime; s_HitPos = tf.position; }
        }
        s_LastHp = hp.current;
    }
    if (s_KnockAt >= 0.0f && m_AutoTime >= s_KnockAt + 0.4f)
    {
        const float dx = tf.position.x - s_HitPos.x, dz = tf.position.z - s_HitPos.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        s_KnockMax = (std::max)(s_KnockMax, dist);
        snprintf(line, sizeof(line), "ghost knock %c dist %.2f lift %.2f", phase, dist, tf.position.y - s_BaseY);
        AutoTestLog(line);
        s_KnockAt = -1.0f;
    }
    if (!s_Look1 && s_FirstHitAt >= 0.0f && m_AutoTime >= s_FirstHitAt + 0.1f)
    {
        snprintf(line, sizeof(line), "ghost look %c1", phase);
        AutoTestLog(line);
        s_Look1 = true;
    }

    // ---- 0.5 秒毎の読み戻し ----
    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.5f;
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        std::vector<Swarm::EnemyExtra> extra;
        int alive = 0, inWall = 0;
        float nearest = 1e9f, hpSum = 0.0f;
        if (m_Swarm.DebugReadEnemies(enemies, states, &extra))
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                ++alive;
                hpSum += Swarm::HpFromFixed(enemies[i].hp);
                int gx, gz;
                m_Grid.WorldToCell(enemies[i].position, gx, gz);
                if (!m_Grid.IsWalkable(gx, gz)) ++inWall;
                const Vector3 d = enemies[i].position - tf.position;
                nearest = (std::min)(nearest, std::sqrt(d.x * d.x + d.z * d.z));
            }
        snprintf(line, sizeof(line), "ghost %c t %.1f alive %d inWall %d nearest %.1f hpAvg %.0f hits %d lost %.0f",
            phase, m_AutoTime, alive, inWall, alive > 0 ? nearest : -1.0f, alive > 0 ? hpSum / alive : 0.0f, s_Hits, s_Lost);
        AutoTestLog(line);
        if (!s_Look0 && ((phase == 'A' && m_AutoTime >= 5.0f) || (phase == 'B' && alive > 0 && nearest < 10.0f)))
        {
            snprintf(line, sizeof(line), "ghost look %c0", phase);
            AutoTestLog(line);
            s_Look0 = true;
        }
    }

    if (m_AutoStep == 1 && m_AutoTime >= 13.0f)
    {
        result('A');
        beginPhase(0, 5);
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 26.0f)
    {
        result('B');
        AutoTestLog("ghost done");
        m_AutoStep = 3;
    }
}

REGISTER_BATTLE_AUTOTEST("ghost", AutoTestGhost)
