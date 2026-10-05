// ============================================================
// AutoTestChest.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=chest
// VFXL_BATTLE_AUTOTEST=chest：魔法書の木箱へ形の違う物をまとめて降らせ、積み方・眠るまでを記録して撮る。
// バックパックが開いて gameplay が止まるので Update から直接呼ぶ
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestChest final : public BattleAutoTest
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
// TEMP-TEST: 魔法書の木箱（VFXL_BATTLE_AUTOTEST=chest）
// 1 秒: 湧き停止、バックパックは火球だけ、魔法書へ形の違う物を 9 個（十字の隕石・縦 2 の矢・L 字の弧…）→ バックパックを開く
// 0.5 秒毎に "chest t bodies contacts asleep"。5 秒 "chest look settled"（外から撮る）
// 6 秒: 4 個追加で積み上げ → 10 秒 "chest look pile" → 11 秒 "chest done"
// ============================================================
void AutoTestChest::Run(float dt)
{
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player)) return;
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;

    static float s_NextLog = 0.0f;
    if (m_AutoStep >= 1 && m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.5f;
        const auto& sb = m_GameUI.GetSpellbook();
        char line[128];
        snprintf(line, sizeof(line), "chest t %.1f bodies %d contacts %d asleep %d maxV %.1f maxW %.3f",
            m_AutoTime, sb.GetBodyCount(), sb.GetContactCount(), sb.IsAsleep() ? 1 : 0,
            sb.GetMaxSpeed(), sb.GetMaxAngSpeed());
        AutoTestLog(line);
    }

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Fireball, lo + 1, lo + 1, 0);
            bp.dirty = true;
        }
        if (m_Registry.Has<SpellbookComponent>(m_Player))
        {
            auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
            for (ItemID id : { ItemID::Meteor, ItemID::GoldenArrow, ItemID::ArcBolt, ItemID::StoneShot,
                ItemID::HomingBolt, ItemID::HasteRune, ItemID::SplitRune, ItemID::Magnifier, ItemID::DoubleCastRune })
                book.Learn(id);
        }
        m_GameUI.TestShow(1);
        AutoTestLog("chest open");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 5.0f)
    {
        AutoTestLog("chest look settled");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 6.0f)
    {
        if (m_Registry.Has<SpellbookComponent>(m_Player))
        {
            auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
            for (ItemID id : { ItemID::Meteor, ItemID::ArcBolt, ItemID::GoldenArrow, ItemID::Fireball })
                book.Learn(id);
        }
        AutoTestLog("chest add 4");
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 10.0f)
    {
        AutoTestLog("chest look pile");
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 11.0f)
    {
        AutoTestLog("chest done");
        m_AutoStep = 5;
    }
}

REGISTER_BATTLE_AUTOTEST("chest", AutoTestChest)
