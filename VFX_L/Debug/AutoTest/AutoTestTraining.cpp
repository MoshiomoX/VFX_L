// ============================================================
// AutoTestTraining.cpp
// TEMP-TEST: VFXL_SPELL_LAB=1 + VFXL_BATTLE_AUTOTEST=training
// トレーニング（実験場）のゲーム内メニュー（2026-10-07）：開く・木箱へ入れる / 全部入れる / 空にする・
//   的・群れ・ボス・全部消す を、メニューと同じ経路（TrainingMenuUI → GameUI → SpellLab::Apply）で押して記録・撮影
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    // TrainingMenuUI::RowId の番号
    constexpr int kRowTargets = 0, kRowSpawn = 5, kRowItems = 6, kRowChest = 7;

    class AutoTestTraining final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        // メニューを開いている間は gameplay が止まるので Update から
        void UpdateFrame(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// 実時間。1 秒: メニューを開く → 1.6 秒 `training look menu`
// 2.0 秒: 絵の行の 1 番目と 4 番目を入れる（木箱に 1 個ずつ）→ 2.3 秒 `training look additem`（結果の一行）
// 2.6 秒: 「全部入れる」→ 3.0 秒: 的（正面 3）・群れ（既定 = 雑魚 30 体、動く）・ボス → 3.3 秒 閉じる
// 0.5 秒毎に `training t alive boss bossHp chest unplaced`。6 秒 `training look swarm`、9 秒 `training look boss`
// 10 秒: 開いて「敵を全部消す」「木箱を空にする」→ 10.3 秒 `training look cleared`（メニュー）→ 10.6 秒 閉じる
// 12 秒 done
// ============================================================
void AutoTestTraining::Run(float dt)
{
    static float s_Real = 0.0f;
    static int s_Step = 0;
    static float s_NextLog = 0.0f;
    s_Real += dt;
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<SpellbookComponent>(m_Player)) return;
    const auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
    const auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    char line[200];

    auto chest = [&](int& total, int& unplaced)
        {
            total = unplaced = 0;
            for (const auto& e : book.entries)
            {
                const ItemCategory c = ItemDatabase::GetCategory(e.id);
                if (c == ItemCategory::Frame) continue;
                total += e.count;
                unplaced += e.count - BackpackLogic::CountPlaced(bp, e.id);
            }
        };
    auto press = [&](int row, int col, const char* what)
        {
            m_GameUI.TestTraining().TestActivate(row, col);
            snprintf(line, sizeof(line), "training press %s (row %d col %d)", what, row, col);
            AutoTestLog(line);
        };

    if (s_Step == 0 && s_Real >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        int total = 0, unplaced = 0;
        chest(total, unplaced);
        snprintf(line, sizeof(line), "training start lab %d chest %d unplaced %d alive %u", m_SpellLab.Enabled() ? 1 : 0,
            total, unplaced, m_Swarm.GetCounters().aliveEnemies);
        AutoTestLog(line);
        m_GameUI.TestOpenTraining(true);
        s_Step = 1;
    }
    else if (s_Step == 1 && s_Real >= 1.6f) { AutoTestLog("training look menu"); s_Step = 2; }
    else if (s_Step == 2 && s_Real >= 2.0f)
    {
        press(kRowItems, 0, "add item 0");
        press(kRowItems, 3, "add item 3");
        s_Step = 3;
    }
    else if (s_Step == 3 && s_Real >= 2.3f)
    {
        int total = 0, unplaced = 0;
        chest(total, unplaced);
        snprintf(line, sizeof(line), "training chest after 2 adds %d unplaced %d", total, unplaced);
        AutoTestLog(line);
        AutoTestLog("training look additem");
        s_Step = 4;
    }
    else if (s_Step == 4 && s_Real >= 2.6f)
    {
        press(kRowChest, 0, "add all");
        int total = 0, unplaced = 0;
        chest(total, unplaced);
        snprintf(line, sizeof(line), "training chest after add all %d unplaced %d", total, unplaced);
        AutoTestLog(line);
        s_Step = 5;
    }
    else if (s_Step == 5 && s_Real >= 3.0f)
    {
        press(kRowTargets, 0, "targets ahead");
        press(kRowSpawn, 0, "swarm");
        press(kRowSpawn, 1, "boss");
        press(kRowSpawn, 1, "boss again (should refuse)");
        s_Step = 6;
    }
    else if (s_Step == 6 && s_Real >= 3.3f)
    {
        m_GameUI.TestOpenTraining(false);
        s_NextLog = s_Real;
        s_Step = 7;
    }
    else if (s_Step == 7)
    {
        static bool s_SwarmShot = false, s_BossShot = false;
        if (!s_SwarmShot && s_Real >= 6.0f) { AutoTestLog("training look swarm"); s_SwarmShot = true; }
        if (!s_BossShot && s_Real >= 9.0f) { AutoTestLog("training look boss"); s_BossShot = true; }
        if (s_Real >= 10.0f)
        {
            m_GameUI.TestOpenTraining(true);
            press(kRowSpawn, 2, "kill all");
            press(kRowChest, 1, "clear chest");
            s_Step = 8;
        }
    }
    else if (s_Step == 8 && s_Real >= 10.3f) { AutoTestLog("training look cleared"); s_Step = 9; }
    else if (s_Step == 9 && s_Real >= 10.6f) { m_GameUI.TestOpenTraining(false); s_Step = 10; }
    else if (s_Step == 10 && s_Real >= 12.0f) { AutoTestLog("training done"); s_Step = 11; }

    if (s_Step >= 7 && s_Step <= 10 && s_Real >= s_NextLog)
    {
        s_NextLog += 0.5f;
        int total = 0, unplaced = 0;
        chest(total, unplaced);
        snprintf(line, sizeof(line), "training t %.2f alive %u boss %d bossHp %.2f chest %d unplaced %d", s_Real,
            m_Swarm.GetCounters().aliveEnemies, m_Stage.IsBossAlive() ? 1 : 0, m_Stage.BossHpRatio(), total, unplaced);
        AutoTestLog(line);
    }
}

REGISTER_BATTLE_AUTOTEST("training", AutoTestTraining)
