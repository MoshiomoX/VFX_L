// ============================================================
// AutoTestBurrower.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=burrower
// 砂潜り（kEnemyKindBurrower。2026-10-17）：正面 12m に 1 体。砂の下を寄って来て、足元で顔を出して 1 回噛み、また潜る（ユーザー「冒头一咬就走」）。
// 潜っている間は撃っても当たらない（HitCS が飛ばす）ことも見る。
// 10-26（ユーザー「潜伏兵基本打不到、改成対地の魔法なら当たる」）：右前 7m に moveSpeed 0 の砂潜り（訓練の的 = ずっと潜ったまま）を置き、
// 2〜5.5 秒に 0.5 秒毎に足元へ地面の範囲（半径 2 / 威力 4）を出す → 潜っていても HP が減るはず（`burrower target …`）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBurrower final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// 1 秒: 湧き停止・全消し・HP 10 万・詠唱停止。正面 +Z 12m に砂潜り 1 体、カメラ背後 9m / 24°
// 1.5 秒: `burrower look buried`（砂の山だけ）。0.25 秒毎に `burrower t fuse dist hp hits`（fuse > 0 = 顔を出している）、
//   初めて顔を出した 0.35 秒後に `burrower look surfaced`。6 秒: 詠唱を戻す（潜っている時は当たらない）。
//   10 秒: `burrower fight alive` + `look fight` + `burrower target result hp0 hp lost`（lost > 0 = 地面の範囲が潜った敵に届いた）。11 秒: done
//   的の HP が初めて減ったら `burrower target hit t hp`、その 0.3 秒後に `burrower look hit`（砂の山の上に数字と HP バー）
// ============================================================
void AutoTestBurrower::Run()
{
    char line[200];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    static float s_NextLog = 1.0f, s_LastHp = 0.0f, s_SurfacedAt = -1.0f;
    static int s_Hits = 0;
    static bool s_LookSurf = false, s_LookBuried = false, s_LookHit = false;
    static float s_NextChip = 2.0f, s_TargetHp0 = -1.0f, s_TargetHitAt = -1.0f, s_TargetHpNow = -1.0f;
    static Vector3 s_TargetPos;

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
        m_Mobs.KindStats(Swarm::kEnemyKindBurrower, hp, speed);
        const float x = tf.position.x, z = tf.position.z + 12.0f;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), hp, speed, Swarm::kEnemyKindBurrower);
        // 右前 7m: 動かない砂潜り（moveSpeed 0 = 顔を出さない）= 地面の範囲で削る的
        const float tx = tf.position.x + 4.0f, tz = tf.position.z + 7.0f;
        s_TargetPos = Vector3(tx, m_Grid.SampleHeight(tx, tz) + groundY, tz);
        m_Swarm.SpawnEnemy(s_TargetPos, hp, 0.0f, Swarm::kEnemyKindBurrower);
        s_TargetHp0 = hp;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(24.0f);
        cam.distance = 9.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "burrower start hp %.0f speed %.2f", hp, speed);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    if (!s_LookBuried && m_AutoTime >= 1.5f) { AutoTestLog("burrower look buried"); s_LookBuried = true; }

    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        if (hp.current < s_LastHp - 0.5f) ++s_Hits;
        s_LastHp = hp.current;
    }
    // 2〜5.5 秒: 的の足元に地面の範囲（エリート自動テストの削りと同じ形）
    if (m_AutoTime >= s_NextChip && m_AutoTime < 5.75f)
    {
        s_NextChip += 0.5f;
        Swarm::Area a;
        a.center = s_TargetPos;
        a.radius = 2.0f;
        a.damage = 4.0f;
        a.tickInterval = 0.2f;
        a.timeLeft = 0.22f;
        m_Swarm.SpawnArea(a);
    }
    if (!s_LookHit && s_TargetHitAt >= 0.0f && m_AutoTime >= s_TargetHitAt + 0.3f) { AutoTestLog("burrower look hit"); s_LookHit = true; }
    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog += 0.25f;
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        std::vector<Swarm::EnemyExtra> ex;
        if (m_Swarm.DebugReadEnemies(en, st, &ex))
        {
            for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
            {
                if (st[i] == Swarm::kStateDead || ex[i].kind != Swarm::kEnemyKindBurrower) continue;
                if (en[i].moveSpeed < 0.01f)
                {
                    // the buried target
                    s_TargetHpNow = Swarm::HpFromFixed(en[i].hp);
                    snprintf(line, sizeof(line), "burrower target t %.2f fuse %.2f hp %.1f", m_AutoTime, ex[i].fuse, s_TargetHpNow);
                    AutoTestLog(line);
                    if (s_TargetHitAt < 0.0f && s_TargetHpNow < s_TargetHp0 - 0.5f)
                    {
                        s_TargetHitAt = m_AutoTime;
                        snprintf(line, sizeof(line), "burrower target hit t %.2f hp %.1f", m_AutoTime, s_TargetHpNow);
                        AutoTestLog(line);
                    }
                    continue;
                }
                const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
                snprintf(line, sizeof(line), "burrower t %.2f fuse %.2f dist %.2f hp %.1f hits %d", m_AutoTime, ex[i].fuse,
                    std::sqrt(dx * dx + dz * dz), Swarm::HpFromFixed(en[i].hp), s_Hits);
                AutoTestLog(line);
                if (ex[i].fuse > 0.0f && s_SurfacedAt < 0.0f) s_SurfacedAt = m_AutoTime;
                // 10-26: break -> continue, the buried target comes after the walker in the pool
            }
        }
    }
    if (!s_LookSurf && s_SurfacedAt >= 0.0f && m_AutoTime >= s_SurfacedAt + 0.35f) { AutoTestLog("burrower look surfaced"); s_LookSurf = true; }
    if (m_AutoStep == 1 && m_AutoTime >= 6.0f)
    {
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        m_AutoStep = 2;
    }
    if (m_AutoStep == 2 && m_AutoTime >= 10.0f)
    {
        snprintf(line, sizeof(line), "burrower fight alive %u hits %d", m_Swarm.GetCounters().aliveEnemies, s_Hits);
        AutoTestLog(line);
        AutoTestLog("burrower look fight");
        snprintf(line, sizeof(line), "burrower target result hp0 %.1f hp %.1f lost %.1f", s_TargetHp0, s_TargetHpNow, s_TargetHp0 - s_TargetHpNow);
        AutoTestLog(line);
        m_AutoStep = 3;
    }
    if (m_AutoStep == 3 && m_AutoTime >= 11.0f) { AutoTestLog("burrower done"); m_AutoStep = 4; }
}

REGISTER_BATTLE_AUTOTEST("burrower", AutoTestBurrower)
