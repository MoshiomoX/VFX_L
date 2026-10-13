// ============================================================
// AutoTestTutorial.cpp
// TEMP-TEST: VFXL_TUTORIAL=1 + VFXL_BATTLE_AUTOTEST=tutorial
// 初回のチュートリアル（UI/TutorialGuide、2026-10-13）を段ごとに通す：
//   移動（入力で 2 秒歩く）→ ジャンプ + スライディング → 敵 3 体（HP 1 を 3 体出す。開局のホーミングボルトが倒す）
//   → レベルアップ（経験を足す → 四択を 1 枚目で閉じる）→ バックパック（TestShow(1) → 閉じる）→ 魔力解放（testSurge）
//   → 宝箱・ボスの説明（9 秒ずつ待つ）→ 完了。段が変わるたび `tutorial step <n> t`、その 0.5 秒後 `tutorial look <段>`。
//   終わったら `tutorial done`（GameSettings::tutorialDone が立つ。自動テストの後は Settings.json を元に戻す事）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Core/GameSettings.h"

namespace
{
    class AutoTestTutorial final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        bool SelfClock() const override { return true; }   // バックパック・四択を開いている間も進める
        void UpdateFrame(float dt) override { Run(dt); }

    private:
        void Run(float dt);
        int   m_LastStep = -1;
        float m_StepAt = 0.0f;
        bool  m_Looked = false;
        float m_Acted = -1.0f;      // この段で手を打った時刻（二重にしない）
        bool  m_DoneSaved = false;
    };

    const char* StepName(TutorialGuide::Step s)
    {
        switch (s)
        {
        case TutorialGuide::Step::Move: return "move";
        case TutorialGuide::Step::JumpSlide: return "jumpslide";
        case TutorialGuide::Step::AutoAttack: return "attack";
        case TutorialGuide::Step::LevelUp: return "levelup";
        case TutorialGuide::Step::Backpack: return "backpack";
        case TutorialGuide::Step::Surge: return "surge";
        case TutorialGuide::Step::Crates: return "crates";
        case TutorialGuide::Step::Boss: return "boss";
        case TutorialGuide::Step::Done: return "done";
        default: return "?";
        }
    }
}

void AutoTestTutorial::Run(float dt)
{
    m_AutoTime += dt;
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        char line[96];
        snprintf(line, sizeof(line), "tutorial start active %d step %d", m_Tutorial.Active() ? 1 : 0, (int)m_Tutorial.CurrentStep());
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    const int step = (int)m_Tutorial.CurrentStep();
    if (step != m_LastStep)
    {
        char line[96];
        snprintf(line, sizeof(line), "tutorial step %d %s t %.2f", step, StepName(m_Tutorial.CurrentStep()), m_AutoTime);
        AutoTestLog(line);
        m_LastStep = step;
        m_StepAt = m_AutoTime;
        m_Looked = false;
        m_Acted = -1.0f;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        m_PlayerControlSystem.testSlide = false;
    }
    const float st = m_AutoTime - m_StepAt;
    if (!m_Looked && st >= 0.5f)
    {
        char line[64];
        snprintf(line, sizeof(line), "tutorial look %s", StepName(m_Tutorial.CurrentStep()));
        AutoTestLog(line);
        m_Looked = true;
    }
    if (!m_Tutorial.Active())
    {
        if (!m_DoneSaved)
        {
            char line[96];
            snprintf(line, sizeof(line), "tutorial done t %.2f settingsDone %d", m_AutoTime, GameSettings::Get().tutorialDone ? 1 : 0);
            AutoTestLog(line);
            m_DoneSaved = true;
        }
        return;
    }

    // 段ごとに手を打つ（案内を 1.2 秒見せてから）
    if (st < 1.2f) return;
    switch (m_Tutorial.CurrentStep())
    {
    case TutorialGuide::Step::Move:
        m_PlayerControlSystem.testMove = Vector2(0.0f, 1.0f);   // 前へ歩く（4m で次へ）
        break;
    case TutorialGuide::Step::JumpSlide:
        // testJump は自分で下ろす（立てっぱなしだと着地のたびに跳び、滑れない）
        if (m_Acted < 0.0f) { m_PlayerControlSystem.testJump = true; m_Acted = m_AutoTime; }
        else
        {
            m_PlayerControlSystem.testJump = false;
            if (m_AutoTime - m_Acted > 1.2f) { m_PlayerControlSystem.testMove = Vector2(0.0f, 1.0f); m_PlayerControlSystem.testSlide = true; }
        }
        break;
    case TutorialGuide::Step::AutoAttack:
        if (m_Acted < 0.0f)
        {
            // 正面 6m に HP 1 の雑魚を 3 体（開局のホーミングボルトが倒す）
            const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
            const float gy = m_Swarm.GetAIParams().groundY;
            for (float x : { -1.0f, 0.0f, 1.0f })
                m_Swarm.SpawnEnemy(Vector3(pp.x + x, gy, pp.z + 6.0f), 1.0f, 0.0f);
            m_Acted = m_AutoTime;
        }
        break;
    case TutorialGuide::Step::LevelUp:
        if (m_Acted < 0.0f && m_Registry.Has<LevelComponent>(m_Player))
        {
            auto& lv = m_Registry.Get<LevelComponent>(m_Player);
            lv.experience += lv.ExpToNext() + 1.0f;
            m_Acted = m_AutoTime;
        }
        else if (m_Acted >= 0.0f && m_AutoTime - m_Acted > 1.5f && m_Registry.Has<LevelComponent>(m_Player))
        {
            const auto& lv = m_Registry.Get<LevelComponent>(m_Player);
            if (lv.IsChoosing()) LevelUpSystem::Choose(m_Registry, m_Player, lv.pendingChoices.front());   // 1 枚目を選ぶ
        }
        break;
    case TutorialGuide::Step::Backpack:
        if (m_Acted < 0.0f) { m_GameUI.TestShow(1); m_Acted = m_AutoTime; }
        else if (m_AutoTime - m_Acted > 1.5f && m_GameUI.IsBackpackOpen()) m_GameUI.TestShow(0);
        break;
    case TutorialGuide::Step::Surge:
        if (m_Acted < 0.0f) { m_PlayerControlSystem.testSurge = true; m_Acted = m_AutoTime; }
        break;
    default:
        break;   // 宝箱・ボス・完了：待つだけ
    }
    if (m_AutoTime >= 90.0f && !m_DoneSaved) { AutoTestLog("tutorial done (timeout)"); m_DoneSaved = true; }
}

REGISTER_BATTLE_AUTOTEST("tutorial", AutoTestTutorial)
