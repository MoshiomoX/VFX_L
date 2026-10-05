// ============================================================
// AutoTestBalance.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=balance
// VFXL_BATTLE_AUTOTEST=balance：無敵・入力なしで普通に遊ばせ（三択は先頭を自動で選ぶ）、
// 5 秒毎に経過時間・雑魚数・撃破・等級・HP・難度の倍率を記録。
// 10 秒で経過時間を 170 秒（3:00 のエリートが出る）、30 秒で 5 分、45 秒で 10 分（時間切れ）、55 秒で 11 分へ飛ばす
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBalance final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateFrame(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: 難度の推移（VFXL_BATTLE_AUTOTEST=balance）
// 実時間で数える（三択・バックパックの間は gameplay が止まるので Update から呼ぶ）。
// 0 秒: 無敵。5 秒毎に 1 行。10 秒: 経過時間を 170 秒へ（3:00 のエリート）、30 秒: 300 秒へ、
// 45 秒: 600 秒へ（8:00 のエリート・時間切れ → 最終ウェーブ）、55 秒: 660 秒へ（最終ウェーブ 3 段）。
// 出来事は "balance event"、その 5 秒後に "balance look"。70 秒で "balance done"
// ============================================================
void AutoTestBalance::Run(float dt)
{
    static float s_Real = 0.0f, s_Log = 0.0f;
    static int s_Jump = 0;
    s_Real += dt;
    if (!m_Registry.IsValid(m_Player)) return;

    auto& hp = m_Registry.Get<HealthComponent>(m_Player);
    hp.invincible = true;

    // 三択が出ていたら先頭を選ぶ
    if (m_Registry.Has<LevelComponent>(m_Player))
    {
        const auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        if (lv.IsChoosing())
        {
            const ItemID pick = lv.pendingChoices.front();
            LevelUpSystem::Choose(m_Registry, m_Player, pick);
            const ItemCommon* ic = ItemDatabase::GetCommon(pick);
            char line[128];
            snprintf(line, sizeof(line), "balance pick %s (level %d)", ic ? ic->name : "?", lv.level);
            AutoTestLog(line);
        }
    }

    if (s_Jump == 0 && s_Real >= 10.0f) { m_RunTime = 170.0f; s_Jump = 1; AutoTestLog("balance jump to 170 s"); }
    if (s_Jump == 1 && s_Real >= 30.0f) { m_RunTime = 300.0f; s_Jump = 2; AutoTestLog("balance jump to 300 s"); }
    if (s_Jump == 2 && s_Real >= 45.0f) { m_RunTime = 600.0f; s_Jump = 3; AutoTestLog("balance jump to 600 s"); }
    if (s_Jump == 3 && s_Real >= 55.0f) { m_RunTime = 660.0f; s_Jump = 4; AutoTestLog("balance jump to 660 s"); }

    // 時間で起きた出来事（エリートなど）。出てから 5 秒後にも 1 行（歩いて来たところを外から撮る）
    static float s_EventAt = -1.0f;
    if (const char* ev = m_Stage.ConsumeEvent())
    {
        char line[96];
        snprintf(line, sizeof(line), "balance event %s at run %.0f", ev, m_RunTime);
        AutoTestLog(line);
        s_EventAt = s_Real;
    }
    if (s_EventAt >= 0.0f && s_Real >= s_EventAt + 5.0f)
    {
        AutoTestLog("balance look");
        s_EventAt = -1.0f;
    }

    s_Log += dt;
    if (s_Log >= 5.0f)
    {
        s_Log = 0.0f;
        const auto& c = m_Swarm.GetCounters();
        const auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        char line[240];
        snprintf(line, sizeof(line),
            "balance real %.0f run %.0f mobs %u kills %u level %d exp %.0f/%.0f hp %.0f/%.0f statMul %.2f spawn/s %.2f contact %.1f orbs %u",
            s_Real, m_RunTime, c.aliveEnemies, c.killCount, lv.level, lv.experience, lv.ExpToNext(),
            hp.current, hp.max, m_Mobs.GetHpMul(), m_Mobs.Director().spawnPerSecond,
            m_Swarm.GetAIParams().contactDamage, c.aliveOrbs);
        AutoTestLog(line);
    }
    if (s_Real >= 70.0f && s_Jump == 4) { AutoTestLog("balance done"); s_Jump = 5; }
}

REGISTER_BATTLE_AUTOTEST("balance", AutoTestBalance)
