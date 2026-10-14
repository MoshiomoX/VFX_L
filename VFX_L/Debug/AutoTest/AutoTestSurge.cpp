// ============================================================
// AutoTestSurge.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=surge
// VFXL_BATTLE_AUTOTEST=surge：魔力解放（Q）で 3 秒間 MP が減らず撃ち続けられるか・再使用待ちで 2 回目が効かないか
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestSurge final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 魔力解放（VFXL_BATTLE_AUTOTEST=surge、2026-10-01）
// 1 秒: 湧き停止・全消し・無敵、正面 10m に動かない的 3 体（HP 10 万）。MP 30 / 100、回復 0（減る一方にする）。
//   開始時の追尾弾が自動で撃って MP を使い切る。
// 3 秒: Q（testSurge）→ 3 秒間 MP が減らずに撃ち続けるはず。5 秒: もう一度 Q → 再使用待ちなので何も起きないはず。
// 0.25 秒毎に `surge t mp surge cooldown proj`（proj = 飛んでいる弾の数。MP が尽きると 0 に落ち、
// 解放中は MP が減らないまま撃ち続けて戻るはず）。
// 2.5 秒 `surge look ready`（使える状態の大きな欄）、4 秒 `surge look on`（金の MP バー・欄）、8 秒 `surge look cooldown`、
// 再使用待ちが明けたら（20 秒、2026-10-04）`surge ready again t`、0.1 秒後 `surge look flash`（明けた瞬間の光）、その 1 秒後 done
// ============================================================
void AutoTestSurge::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (!m_Registry.Has<ManaComponent>(m_Player)) return;
    auto& mp = m_Registry.Get<ManaComponent>(m_Player);
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;
    static float s_NextLog = 0.0f;
    static int s_Phase = 0;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float x : { -1.5f, 0.0f, 1.5f })
            m_Swarm.SpawnEnemy(Vector3(pp.x + x, gy, pp.z + 10.0f), 100000.0f, 0.0f);
        mp.max = 100.0f;
        mp.current = 30.0f;
        mp.regen = 0.0f;
        mp.surgeTime = mp.surgeCooldownLeft = 0.0f;
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        AutoTestLog("surge start: mp 30/100 regen 0, Q at 3s, Q again at 5s");
        s_NextLog = m_AutoTime;
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1) return;

    static float s_ReadyAt = 0.0f;
    if (s_Phase == 0 && m_AutoTime >= 2.5f) { AutoTestLog("surge look ready"); s_Phase = 20; }        // 押す前（使える欄）
    else if (s_Phase == 20 && m_AutoTime >= 3.0f) { m_PlayerControlSystem.testSurge = true; AutoTestLog("surge press 1"); s_Phase = 1; }
    else if (s_Phase == 1 && m_AutoTime >= 3.15f) { AutoTestLog("surge look burst"); s_Phase = 10; }   // 始まりの金の爆発
    else if (s_Phase == 10 && m_AutoTime >= 4.0f) { AutoTestLog("surge look on"); s_Phase = 2; }      // 体の光 + 画面の金の縁
    else if (s_Phase == 2 && m_AutoTime >= 5.0f) { m_PlayerControlSystem.testSurge = true; AutoTestLog("surge press 2 (cooldown)"); s_Phase = 3; }
    else if (s_Phase == 3 && m_AutoTime >= 8.0f) { AutoTestLog("surge look cooldown"); s_Phase = 4; }
    else if (s_Phase == 4 && mp.surgeCooldownLeft <= 0.0f)
    {
        char line[80];
        snprintf(line, sizeof(line), "surge ready again t %.2f (pressed at 3.00, cooldown %.0f)", m_AutoTime, mp.surgeCooldown);
        AutoTestLog(line);
        s_ReadyAt = m_AutoTime;
        s_Phase = 5;
    }
    else if (s_Phase == 5 && m_AutoTime >= s_ReadyAt + 0.1f) { AutoTestLog("surge look flash"); s_Phase = 6; }   // 明けた瞬間の光
    else if (s_Phase == 6 && m_AutoTime >= s_ReadyAt + 1.1f)
    {
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("surge done");
        m_AutoStep = 2;
        return;
    }

    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.25f;
        char line[160];
        snprintf(line, sizeof(line), "surge t %.2f mp %.1f surge %.2f cooldown %.1f proj %u",
            m_AutoTime, mp.current, mp.surgeTime, mp.surgeCooldownLeft, m_Swarm.GetCounters().aliveProjectiles);
        AutoTestLog(line);
    }
}

REGISTER_BATTLE_AUTOTEST("surge", AutoTestSurge)
