// ============================================================
// AutoTestBossDrama.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=bossdrama（2026-10-29、Boss の狂暴の咆哮・倒した瞬間の演出）
//   1 秒: 無敵・湧き停止・全消し・詠唱停止、Boss の技は切る（咆哮は技と別なので出る）、HP 3000 の止まった Boss を呼ぶ。
//   湧いたら：プレイヤーを Boss から 14m、カメラは背中から Boss へ。Boss の周り 6〜22m に動かない雑魚 24 体（HP 50）。
//   A: Boss の所に範囲を置いて削る → HP が 45% を切ったら止める → 咆哮を待つ
//      `bossdrama roar t ratio`、`bossdrama look roar`（吠えて 0.35 秒）、吠え終わって 1 秒 `bossdrama rage aura <0|1>`
//   B: また削って倒す → `bossdrama defeat slow <秒> wave <m> gold <0|1> alive kills orbs`、
//      `bossdrama look defeat0 / defeat1 / defeat2`（倒して gameplay の 0.05 / 0.25 / 1.5 秒）
//      倒して 2.5 秒 `bossdrama after alive kills(+n) orbs` → done
// 期待：吠える（1 回）・火の粉が付く、倒した瞬間にスロー、波で雑魚 24 体が全部砕けて経験球が出る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBossDrama final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Chip();
        float m_NextChip = 0.0f, m_RoarAt = -1.0f, m_RoarEndAt = -1.0f, m_DefeatAt = -1.0f;
        uint32_t m_KillsAtDefeat = 0;
        int   m_Looks = 0;
        bool  m_RageLogged = false, m_AfterLogged = false, m_RoarShot = false;
    };
}

// Boss の所に小さな範囲を置いて削る（周りの雑魚には届かない）
void AutoTestBossDrama::Chip()
{
    if (m_AutoTime < m_NextChip || !m_Stage.IsBossAlive()) return;
    m_NextChip = m_AutoTime + 0.2f;
    Swarm::Area a;
    a.center = m_Stage.BossPos();
    a.radius = 3.0f;
    a.damage = 150.0f;
    a.tickInterval = 0.2f;
    a.timeLeft = 0.22f;
    m_Swarm.SpawnArea(a);
}

void AutoTestBossDrama::Run()
{
    char line[200];
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
        m_BossAttacks.enabled = false;
        m_Stage.bossHp = 3000.0f;
        const bool ok = m_Stage.RequestBoss(false);
        snprintf(line, sizeof(line), "bossdrama start requested %d type %s", ok ? 1 : 0, BossTypeName(m_BossAttacks.Type()));
        AutoTestLog(line);
        m_AutoStep = 1;
        return;
    }
    if (m_AutoStep == 1 && m_Stage.IsBossAlive() && m_Stage.BossPos().LengthSquared() > 0.01f)
    {
        const Vector3 boss = m_Stage.BossPos();
        Vector3 dir = tf.position - boss; dir.y = 0.0f;
        if (dir.LengthSquared() < 1e-2f) dir = Vector3(0.0f, 0.0f, 1.0f);
        dir.Normalize();
        const Vector3 p = boss + dir * 14.0f;
        tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
        if (m_Registry.Has<RigidbodyComponent>(m_Player)) m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(std::atan2(dir.x, -dir.z) * 57.29578f);   // 背中から Boss の方
        cam.SetPitch(35.0f);
        cam.distance = 20.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        // Boss の周りに動かない雑魚（倒した時の波で砕けるか）
        int placed = 0;
        for (int i = 0; i < 24; ++i)
        {
            const float a = 6.2831853f * (float)i / 24.0f + 0.13f;
            const float r = 6.0f + 16.0f * (float)((i * 7) % 24) / 23.0f;
            const float x = boss.x + std::cos(a) * r, z = boss.z + std::sin(a) * r;
            int gx = 0, gz = 0;
            m_Grid.WorldToCell(Vector3(x, 0.0f, z), gx, gz);
            if (!m_Grid.IsWalkable(gx, gz)) continue;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z), z), 50.0f, 0.0f);
            ++placed;
        }
        snprintf(line, sizeof(line), "bossdrama setup mobs %d", placed);
        AutoTestLog(line);
        m_AutoStep = 2;
        return;
    }
    if (m_AutoStep < 2) return;

    const float ratio = m_Stage.BossHpRatio();
    // A: 45% まで削って止める → 咆哮を待つ
    if (m_AutoStep == 2)
    {
        Chip();
        if (m_Stage.IsBossAlive() && ratio > 0.0f && ratio < 0.45f) m_AutoStep = 3;
    }
    if (m_AutoStep == 3)
    {
        if (m_RoarAt < 0.0f && m_BossAttacks.IsRoaring())
        {
            m_RoarAt = m_AutoTime;
            snprintf(line, sizeof(line), "bossdrama roar t %.2f ratio %.2f", m_AutoTime, ratio);
            AutoTestLog(line);
        }
        if (m_RoarAt >= 0.0f && m_RoarEndAt < 0.0f && !m_BossAttacks.IsRoaring()) m_RoarEndAt = m_AutoTime;
        if (m_RoarEndAt >= 0.0f && !m_RageLogged && m_AutoTime >= m_RoarEndAt + 1.0f)
        {
            m_RageLogged = true;
            snprintf(line, sizeof(line), "bossdrama rage aura %d enraged %d roarDone %d", m_BossRageVfx != 0 ? 1 : 0,
                m_BossAttacks.IsEnraged() ? 1 : 0, m_BossAttacks.EnrageRoarDone() ? 1 : 0);
            AutoTestLog(line);
            m_AutoStep = 4;
        }
        if (m_RoarAt < 0.0f && m_AutoTime > 30.0f) { AutoTestLog("bossdrama roar timeout"); m_AutoStep = 4; }
    }
    // B: 倒す
    if (m_AutoStep == 4)
    {
        Chip();
        if (m_Stage.IsBossDefeated() && m_DefeatAt < 0.0f)
        {
            m_DefeatAt = m_AutoTime;
            const auto& c = m_Swarm.GetCounters();
            m_KillsAtDefeat = c.killCount;
            snprintf(line, sizeof(line), "bossdrama defeat t %.2f slow %.2f wave %.1f gold %d alive %u kills %u orbs %u",
                m_AutoTime, m_DefeatSlowLeft, m_DefeatWaveR, m_Stage.AnnounceGold() ? 1 : 0, c.aliveEnemies, c.killCount, c.aliveOrbs);
            AutoTestLog(line);
            m_AutoStep = 5;
        }
    }
    if (m_AutoStep == 5)
    {
        const float t = m_AutoTime - m_DefeatAt;
        auto look = [&](int idx, float at, const char* name) {
            if (m_Looks == idx && t >= at) { snprintf(line, sizeof(line), "bossdrama look %s slow %.2f wave %.1f", name, m_DefeatSlowLeft, m_DefeatWaveR); AutoTestLog(line); ++m_Looks; }
        };
        look(0, 0.05f, "defeat0");
        look(1, 0.25f, "defeat1");
        look(2, 1.5f, "defeat2");
        if (!m_AfterLogged && t >= 2.5f)
        {
            m_AfterLogged = true;
            const auto& c = m_Swarm.GetCounters();
            snprintf(line, sizeof(line), "bossdrama after alive %u kills +%u orbs %u goldChests %d", c.aliveEnemies, c.killCount - m_KillsAtDefeat, c.aliveOrbs, (int)m_Crates.FreeCount());
            AutoTestLog(line);
            AutoTestLog("bossdrama done");
            m_AutoStep = 6;
        }
    }
    // 吠えの見た目（gameplay の秒で 0.35 秒後）
    if (m_RoarAt >= 0.0f && !m_RoarShot && m_AutoTime >= m_RoarAt + 0.35f)
    {
        m_RoarShot = true;
        AutoTestLog("bossdrama look roar");
    }
}

REGISTER_BATTLE_AUTOTEST("bossdrama", AutoTestBossDrama)
