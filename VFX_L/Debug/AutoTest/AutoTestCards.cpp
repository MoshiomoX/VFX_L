// ============================================================
// AutoTestCards.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=cards
// VFXL_BATTLE_AUTOTEST=cards：説明カードの文の長さ（2026-10-09 ユーザー「魔法の説明が長すぎる」）。
//   4 択に魔法・ルーン・召喚物を 4 枚ずつ並べて撮り、最後にバックパックに置いた物の説明（発動中 / 連鎖）も撮る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestCards final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        // 4 択とバックパックの間は gameplay が止まるので Update から呼ぶ
        void UpdateFrame(float dt) override { Run(dt); }
        bool SelfClock() const override { return true; }

    private:
        void Run(float dt);
    };

    // 4 枚ずつ。全部の魔法・ルーン・召喚物と、能力アップを少し
    const ItemID kGroups[][4] = {
        { ItemID::Fireball, ItemID::ArcBolt, ItemID::HomingBolt, ItemID::StoneShot },
        { ItemID::GoldenArrow, ItemID::IceLance, ItemID::Poison, ItemID::Meteor },
        { ItemID::Beam, ItemID::CrystalBall, ItemID::SplitRune, ItemID::Magnifier },
        { ItemID::DoubleCastRune, ItemID::HasteRune, ItemID::Frame3x3, ItemID::ShieldUp },
        { ItemID::IceSpike, ItemID::Meteor, ItemID::Beam, ItemID::IceLance },   // 上級魔法（アイススパイク 2026-10-10）
    };
    constexpr int kGroupCount = (int)(sizeof(kGroups) / sizeof(kGroups[0]));
}

// ============================================================
// 1 秒: 湧き停止・全消し。以後 1.5 秒ごとに 4 択を差し替え、0.8 秒後 `cards look g<n>`。
// 全部撮ったら 4 択を閉じ、バックパックを全面の枠にして 隕石 + 火球 + 石弾（隕石が発動中）と毒を置いて開き、
// 隕石・火球・毒の説明をそれぞれ出して `cards look meteor / fireball / poison`、`cards done`
// ============================================================
void AutoTestCards::Run(float dt)
{
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<LevelComponent>(m_Player)
        || !m_Registry.Has<BackpackComponent>(m_Player)) return;
    auto& lv = m_Registry.Get<LevelComponent>(m_Player);
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    lv.experience = 0.0f;   // 勝手に 4 択が出ないように
    static int s_Meteor = -1, s_Fire = -1, s_Poison = -1;
    char line[160];

    const float groupsStart = 1.5f;
    const float groupStep = 1.5f;
    const float bagAt = groupsStart + groupStep * kGroupCount;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_AutoStep = 1;
    }
    // ---- 4 択 ----
    else if (m_AutoStep >= 1 && m_AutoStep <= kGroupCount * 2)
    {
        const int g = (m_AutoStep - 1) / 2;
        const bool show = ((m_AutoStep - 1) % 2) == 0;
        const float t0 = groupsStart + groupStep * g;
        if (show && m_AutoTime >= t0)
        {
            lv.ClearChoices();
            lv.pendingChoices.assign(std::begin(kGroups[g]), std::end(kGroups[g]));
            ++m_AutoStep;
        }
        else if (!show && m_AutoTime >= t0 + 0.8f)
        {
            snprintf(line, sizeof(line), "cards look g%d", g);
            AutoTestLog(line);
            ++m_AutoStep;
        }
    }
    // ---- バックパックに置いた物 ----
    else if (m_AutoStep == kGroupCount * 2 + 1 && m_AutoTime >= bagAt)
    {
        lv.ClearChoices();
        bp.frames.clear();
        BackpackLogic::RebuildFrameOccupancy(bp);
        for (int r = 0; r < BackpackComponent::GRID; ++r)
            for (int c = 0; c < BackpackComponent::GRID; ++c)
                BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
        ClearBackpackItems(bp);
        s_Meteor = BackpackLogic::Place(bp, ItemID::Meteor, 2, 2, 0);    // 十字 (1,2)(2,1)(2,2)(2,3)(3,2)
        s_Fire = BackpackLogic::Place(bp, ItemID::Fireball, 0, 3, 0);    // (1,3) の左 = 隕石
        BackpackLogic::Place(bp, ItemID::StoneShot, 3, 1, 0);            // (3,1) の上 = 隕石
        s_Poison = BackpackLogic::Place(bp, ItemID::Poison, 1, 7, 0);
        BackpackLogic::Place(bp, ItemID::Magnifier, 1, 1, 0);           // 斜め下 (2,2) = 隕石の中心 → 爆発の半径の行が出るはず
        bp.dirty = true;
        m_GameUI.TestShow(1);
        snprintf(line, sizeof(line), "cards bag meteor %d fire %d poison %d", s_Meteor, s_Fire, s_Poison);
        AutoTestLog(line);
        m_AutoStep = kGroupCount * 2 + 2;
    }
    else if (m_AutoStep == kGroupCount * 2 + 2 && m_AutoTime >= bagAt + 0.6f)
    {
        // 置いた後に index がずれないよう、種類で探し直す
        for (int i = 0; i < (int)bp.items.size(); ++i)
        {
            if (bp.items[i].id == ItemID::Meteor) s_Meteor = i;
            if (bp.items[i].id == ItemID::Fireball) s_Fire = i;
            if (bp.items[i].id == ItemID::Poison) s_Poison = i;
        }
        if (s_Meteor >= 0) m_GameUI.TestTooltip(s_Meteor, { 820.0f, 200.0f });
        m_AutoStep = kGroupCount * 2 + 3;
    }
    else if (m_AutoStep == kGroupCount * 2 + 3 && m_AutoTime >= bagAt + 1.3f) { AutoTestLog("cards look meteor"); m_AutoStep++; }
    else if (m_AutoStep == kGroupCount * 2 + 4 && m_AutoTime >= bagAt + 1.6f)
    {
        if (s_Fire >= 0) m_GameUI.TestTooltip(s_Fire, { 820.0f, 200.0f });
        m_AutoStep++;
    }
    else if (m_AutoStep == kGroupCount * 2 + 5 && m_AutoTime >= bagAt + 2.3f) { AutoTestLog("cards look fireball"); m_AutoStep++; }
    else if (m_AutoStep == kGroupCount * 2 + 6 && m_AutoTime >= bagAt + 2.6f)
    {
        if (s_Poison >= 0) m_GameUI.TestTooltip(s_Poison, { 820.0f, 200.0f });
        m_AutoStep++;
    }
    else if (m_AutoStep == kGroupCount * 2 + 7 && m_AutoTime >= bagAt + 3.3f) { AutoTestLog("cards look poison"); m_AutoStep++; }
    else if (m_AutoStep == kGroupCount * 2 + 8 && m_AutoTime >= bagAt + 3.6f)
    {
        m_GameUI.TestTooltip(-1, { 0.0f, 0.0f });
        AutoTestLog("cards done");
        m_AutoStep++;
    }
}

REGISTER_BATTLE_AUTOTEST("cards", AutoTestCards)
