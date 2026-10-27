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
        // 2026-10-27：蓋の無い箱。もっと入れて、箱の縁から画面へこぼれるか見る
        if (m_Registry.Has<SpellbookComponent>(m_Player))
        {
            auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
            for (int k = 0; k < 4; ++k)
                for (ItemID id : { ItemID::Meteor, ItemID::StoneShot, ItemID::IceLance, ItemID::Poison, ItemID::Fireball })
                    book.Learn(id);
        }
        AutoTestLog("chest add 20");
        m_AutoStep = 5;
    }
    else if (m_AutoStep == 5 && m_AutoTime >= 17.0f)
    {
        AutoTestLog("chest look overflow");
        m_AutoStep = 6;
    }
    else if (m_AutoStep == 6 && m_AutoTime >= 18.0f)
    {
        // 2026-10-27：マウスで離したのと同じ落とし所（画面の真ん中より少し右上・縦向き）に黄金の矢を 1 本出す → その場から落ちるか
        auto& sb = m_GameUI.GetSpellbookMutable();
        sb.TestDropHint(ItemID::GoldenArrow, { m_ScreenW * 0.55f, m_ScreenH * 0.22f }, 1);
        if (m_Registry.Has<SpellbookComponent>(m_Player))
            m_Registry.Get<SpellbookComponent>(m_Player).Learn(ItemID::GoldenArrow);
        AutoTestLog("chest drop hint");
        m_AutoStep = 7;
    }
    else if (m_AutoStep == 7 && m_AutoTime >= 18.1f)
    {
        auto& sb = m_GameUI.GetSpellbookMutable();
        uint32_t uid = 0;
        DirectX::SimpleMath::Vector2 pos;
        char line[128];
        if (sb.GetBodyInfo(sb.GetBodyCount() - 1, uid, pos))
        {
            snprintf(line, sizeof(line), "chest drop spawned at %.0f,%.0f (hint %.0f,%.0f)", pos.x, pos.y, m_ScreenW * 0.55f, m_ScreenH * 0.22f);
            AutoTestLog(line);
        }
        AutoTestLog("chest look drop0");
        m_AutoStep = 8;
    }
    else if (m_AutoStep == 8 && m_AutoTime >= 19.0f)
    {
        AutoTestLog("chest look drop1");
        m_AutoStep = 9;
    }
    else if (m_AutoStep == 9 && m_AutoTime >= 19.5f)
    {
        AutoTestLog("chest done");
        m_AutoStep = 10;
    }
}

REGISTER_BATTLE_AUTOTEST("chest", AutoTestChest)
