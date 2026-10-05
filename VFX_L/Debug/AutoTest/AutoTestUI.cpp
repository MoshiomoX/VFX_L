// ============================================================
// AutoTestUI.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=ui
// VFXL_BATTLE_AUTOTEST=ui：バックパック → tooltip → 一時停止 → HUD → 三択 を順に開いて記録する（画面は外から連写）。
// バックパック・一時停止で gameplay が止まるので Update から直接呼ぶ
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestUI final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateFrame(float dt) override { Run(dt); }
        bool SelfClock() const override { return true; }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: 幻想 UI の見た目（VFXL_BATTLE_AUTOTEST=ui）
// 1 秒: 湧き停止、バックパックへ火球・隕石・拡大鏡・分裂のルーンを置く（MP 無限、経験値 0）
// 2 秒: バックパックを開く "ui backpack" / 4 秒: 拡大鏡の tooltip "ui tooltip" / 6 秒: 一時停止 "ui pause"
// 8 秒: 全部閉じる "ui hud" / 10 秒: 経験値を渡して三択 "ui levelup" / 13 秒 "ui done"
// バックパック・一時停止の間は gameplay が止まるので Update から呼ぶ（dt は止まっていても進む）
// ============================================================
void AutoTestUI::Run(float dt)
{
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player)) return;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (m_AutoStep < 5 && m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;

    static int s_MagnifierIndex = -1;
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
            BackpackLogic::Place(bp, ItemID::Meteor, lo + 2, lo + 2, 0);
            s_MagnifierIndex = BackpackLogic::Place(bp, ItemID::Magnifier, lo + 1, lo + 1, 0);
            BackpackLogic::Place(bp, ItemID::SplitRune, lo, lo + 2, 0);
            bp.dirty = true;
        }
        // 魔法書（バックパックの横の箱）にも置いていない物を入れておく
        if (m_Registry.Has<SpellbookComponent>(m_Player))
        {
            auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
            book.Learn(ItemID::ArcBolt);
            book.Learn(ItemID::HomingBolt);
            book.Learn(ItemID::DoubleCastRune);
            book.Learn(ItemID::Magnifier);
        }
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        // カメラの調整値の保存 → 変更 → 読込 が往復するか（本物の Camera.json には触らない）
        {
            auto& cam = m_Camera.Camera();
            const float before = cam.fov;
            m_Camera.SaveSettings("autotest_camera.json");
            cam.fov = 70.0f;
            m_Camera.LoadSettings("autotest_camera.json");
            char line[128];
            snprintf(line, sizeof(line), "ui camera settings round trip: fov %.1f -> 70 -> %.1f", before, cam.fov);
            AutoTestLog(line);
        }
        AutoTestLog("ui items placed");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.0f)
    {
        m_GameUI.TestShow(1);
        AutoTestLog("ui backpack");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 4.0f)
    {
        m_GameUI.TestTooltip(s_MagnifierIndex, { m_ScreenW * 0.62f, m_ScreenH * 0.30f });
        AutoTestLog("ui tooltip");
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 6.0f)
    {
        m_GameUI.TestTooltip(-1, { 0.0f, 0.0f });
        m_GameUI.TestShow(2);
        AutoTestLog("ui pause");
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 8.0f)
    {
        m_GameUI.TestShow(0);
        AutoTestLog("ui hud");
        m_AutoStep = 5;
    }
    else if (m_AutoStep == 5 && m_AutoTime >= 10.0f && m_Registry.Has<LevelComponent>(m_Player))
    {
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        lv.experience += lv.ExpToNext() + 1.0f;
        AutoTestLog("ui levelup");
        m_AutoStep = 6;
    }
    else if (m_AutoStep == 6 && m_AutoTime >= 10.5f && m_Registry.Has<LevelComponent>(m_Player))
    {
        // 三択の中身を新しい能力値のカードに差し替える（見た目の確認用）
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        if (lv.IsChoosing())
        {
            lv.pendingChoices = { ItemID::MoveSpeedUp, ItemID::JumpPowerUp, ItemID::MaxHealthUp };
            AutoTestLog("ui levelup choices: MoveSpeedUp / JumpPowerUp / MaxHealthUp");
        }
        m_AutoStep = 7;
    }
    else if (m_AutoStep == 7 && m_AutoTime >= 12.0f && m_Registry.Has<PlayerStatsComponent>(m_Player))
    {
        // 実際に選んで、速さと跳ぶ力が上がるかを記録する
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        auto& st = m_Registry.Get<PlayerStatsComponent>(m_Player);
        char line[160];
        snprintf(line, sizeof(line), "ui stat before: moveSpeed %.3f jumpPower %.3f", st.moveSpeed, st.jumpPower);
        AutoTestLog(line);
        LevelUpSystem::Choose(m_Registry, m_Player, ItemID::MoveSpeedUp);
        lv.pendingChoices = { ItemID::JumpPowerUp };
        LevelUpSystem::Choose(m_Registry, m_Player, ItemID::JumpPowerUp);
        snprintf(line, sizeof(line), "ui stat after : moveSpeed %.3f jumpPower %.3f", st.moveSpeed, st.jumpPower);
        AutoTestLog(line);
        m_AutoStep = 8;
    }
    else if (m_AutoStep == 8 && m_AutoTime >= 13.0f)
    {
        AutoTestLog("ui done");
        m_AutoStep = 9;
    }
}

REGISTER_BATTLE_AUTOTEST("ui", AutoTestUI)
