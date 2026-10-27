// ============================================================
// AutoTestBossIntro.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=bossintro（2026-10-27、Boss の登場の演出）
//   1 秒: 無敵・湧き停止・全消し・詠唱停止、門の手前 2m へ。1.5 秒: F（門を使う）。
//   使った直後にプレイヤーを門の前 12m（門から見て同じ側）へ移し、カメラは背中から門の方を向く。
//   0.1 秒ごとに `bossintro t alive intro dist along hp` を書く（dist = Boss と門の口の水平距離、along = 出てくる向きへ何 m 出たか）。
//   `bossintro look <名>`：rumble（0.5 秒）/ emerge（湧いた 0.3 秒後）/ walk（湧いた 1.0 秒後）/ stomp（踏み込みの 0.15 秒後）/ after（演出の後 1 秒）
// 期待：門の口の前 2.5m に湧いて、1.4 秒で 6.5m あまりまで歩き、踏み込みで名前が出る。演出の間はプレイヤーに当たらない
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBossIntro final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        float m_PressAt = -1.0f, m_SpawnAt = -1.0f, m_StompAt = -1.0f, m_EndAt = -1.0f, m_NextLog = 0.0f;
        bool  m_WasActive = false;
        Vector3 m_Gate, m_Dir;
        int   m_Looks = 0;
    };
}

void AutoTestBossIntro::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        const Entity portal = m_Stage.GetPortal();
        if (m_Registry.IsValid(portal) && m_Registry.Has<InteractableComponent>(portal))
        {
            const Vector3 pp = m_Registry.Get<InteractableComponent>(portal).basePos;
            Vector3 d = tf.position - pp; d.y = 0.0f;
            if (d.LengthSquared() < 1e-4f) d = Vector3(1, 0, 0);
            d.Normalize();
            tf.position = Vector3(pp.x + d.x * 2.0f, pp.y + 1.0f, pp.z + d.z * 2.0f);
            if (m_Registry.Has<RigidbodyComponent>(m_Player)) m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
            m_Camera.Camera().SnapToTarget();
            AutoTestLog("bossintro at portal");
        }
        else AutoTestLog("bossintro: no portal");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 1.5f)
    {
        m_AutoInteract = true;
        m_PressAt = m_AutoTime;
        AutoTestLog("bossintro press F");
        m_AutoStep = 2;
    }
    if (m_AutoStep < 2) return;

    // 門を使った次のフレームに演出が始まる：その時の門と向きを覚え、プレイヤーを前 12m へ下げてカメラを門へ向ける
    if (m_AutoStep == 2 && m_BossIntro.Active())
    {
        m_Gate = m_BossIntro.Gate();
        const Vector3 toP = tf.position - m_Gate;
        m_Dir = Vector3(toP.x, 0.0f, toP.z);
        m_Dir.Normalize();
        const Vector3 p = m_Gate + m_Dir * 12.0f;
        tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
        if (m_Registry.Has<RigidbodyComponent>(m_Player)) m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(std::atan2(m_Dir.x, -m_Dir.z) * 57.29578f);   // 背中から門の方（SetYaw は x が逆向き）
        cam.SetPitch(24.0f);
        cam.distance = 9.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        char line[160];
        snprintf(line, sizeof(line), "bossintro started gate %.1f,%.1f,%.1f dir %.2f,%.2f", m_Gate.x, m_Gate.y, m_Gate.z, m_Dir.x, m_Dir.z);
        AutoTestLog(line);
        m_AutoStep = 3;
    }
    if (m_AutoStep < 3) return;

    const bool alive = m_Stage.IsBossAlive();
    const bool active = m_BossIntro.Active();
    const float t = m_AutoTime - m_PressAt;
    auto AnnounceOn = [&]() { float a = 0.0f, g = 0.0f; const wchar_t* s = m_Stage.Announce(a, g); return s && s[0] && a > 0.01f; };
    if (alive && m_SpawnAt < 0.0f)
    {
        m_SpawnAt = m_AutoTime;
        // 湧いた所の周りのマスが歩けるか（5x5、門をくぐる向きが上）
        const Vector3 b = m_BossIntro.Spawn();
        int bx = 0, bz = 0;
        m_Grid.WorldToCell(b, bx, bz);
        std::string s;
        for (int dz = 2; dz >= -2; --dz)
        {
            s += " |";
            for (int dx = -2; dx <= 2; ++dx) s += m_Grid.IsWalkable(bx + dx, bz + dz) ? "." : "#";
        }
        char line[256];
        snprintf(line, sizeof(line), "bossintro spawned at %.1f,%.1f,%.1f cell %d,%d walk(+z up)%s", b.x, b.y, b.z, bx, bz, s.c_str());
        AutoTestLog(line);
    }
    if (m_WasActive && !active && m_EndAt < 0.0f) m_EndAt = m_AutoTime;
    // 踏み込み = 名前の案内が出た時（StageDirector の案内）
    if (m_StompAt < 0.0f && m_SpawnAt >= 0.0f && AnnounceOn()) m_StompAt = m_AutoTime;
    m_WasActive = active;

    if (m_AutoTime >= m_NextLog)
    {
        m_NextLog = m_AutoTime + 0.1f;
        const Vector3 b = m_Stage.BossPos();
        const Vector3 d = b - m_Gate;
        char line[200];
        snprintf(line, sizeof(line), "bossintro t %.2f alive %d intro %d dist %.2f along %.2f hp %.2f playerHits %u announce %d",
            t, alive ? 1 : 0, active ? 1 : 0, alive ? std::sqrt(d.x * d.x + d.z * d.z) : -1.0f,
            alive ? d.x * m_Dir.x + d.z * m_Dir.z : 0.0f, m_Stage.BossHpRatio(), m_BossSlamHits,
            AnnounceOn() ? 1 : 0);
        AutoTestLog(line);
    }
    auto look = [&](int idx, bool cond, const char* name) {
        if (m_Looks == idx && cond) { char l[64]; snprintf(l, sizeof(l), "bossintro look %s", name); AutoTestLog(l); ++m_Looks; }
    };
    look(0, t >= 0.5f, "rumble");
    look(1, m_SpawnAt >= 0.0f && m_AutoTime >= m_SpawnAt + 0.3f, "emerge");
    look(2, m_SpawnAt >= 0.0f && m_AutoTime >= m_SpawnAt + 1.0f, "walk");
    look(3, m_StompAt >= 0.0f && m_AutoTime >= m_StompAt + 0.15f, "stomp");
    look(4, m_EndAt >= 0.0f && m_AutoTime >= m_EndAt + 1.0f, "after");
    if (m_Looks >= 5 && m_AutoStep == 3) { AutoTestLog("bossintro done"); m_AutoStep = 4; }
    if (m_AutoStep == 3 && t > 12.0f) { AutoTestLog("bossintro timeout"); AutoTestLog("bossintro done"); m_AutoStep = 4; }
}

REGISTER_BATTLE_AUTOTEST("bossintro", AutoTestBossIntro)
