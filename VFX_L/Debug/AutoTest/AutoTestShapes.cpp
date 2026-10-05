// ============================================================
// AutoTestShapes.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=shapes
// VFXL_BATTLE_AUTOTEST=shapes：基本魔法の多マスの形（2026-10-04）をバックパックに並べ、隕石・光線の誘発が成り立つか・見た目
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestShapes final : public BattleAutoTest
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
// TEMP-TEST: 基本魔法の多マスの形（VFXL_BATTLE_AUTOTEST=shapes、2026-10-04）
// 1 秒: 湧き停止、バックパックを 3x3 枠 9 枚で全面にして、隕石（十字）の上下左右に火球（2x2）と石弾（L 字）、
// 光線（横 3）の上下に追尾弾（横 2）と弧（Z 字）、空いた所に毒（凸字）と黄金の矢（縦 3）を置く。
// 置けたか（index）・各々の占有 / 影響マスの数・隕石と光線が目覚めたか（IsTriggerReady）を記録 → バックパックを開く。
// 2.5 秒 `shapes look backpack`、3 秒に隕石の説明を出して 4 秒 `shapes look tooltip`、
// 4.5 秒 弧を外して光線が眠るか、戻して目覚めるかを記録、5 秒 `shapes done`
// ============================================================
void AutoTestShapes::Run(float dt)
{
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<BackpackComponent>(m_Player)) return;
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    char line[300];
    static int s_Meteor = -1, s_Beam = -1, s_Arc = -1;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        // 全面に枠
        bp.frames.clear();
        BackpackLogic::RebuildFrameOccupancy(bp);
        for (int r = 0; r < BackpackComponent::GRID; ++r)
            for (int c = 0; c < BackpackComponent::GRID; ++c)
                BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
        ClearBackpackItems(bp);

        struct P { ItemID id; int r, c; const char* name; };
        const P layout[] = {
            { ItemID::Meteor, 2, 2, "meteor" },       // (1,2)(2,1)(2,2)(2,3)(3,2)
            { ItemID::Fireball, 0, 3, "fireball" },   // (0,3)(0,4)(1,3)(1,4)：(1,3) の左 = 隕石 (1,2)
            { ItemID::StoneShot, 3, 1, "stone" },     // (3,1)(4,1)(4,2)：(3,1) の上 = 隕石 (2,1)
            { ItemID::Beam, 6, 5, "beam" },           // (6,4)(6,5)(6,6)
            { ItemID::HomingBolt, 5, 4, "homing" },   // (5,4)(5,5)：真下が光線
            { ItemID::ArcBolt, 7, 6, "arc" },         // (7,6)(7,7)(8,7)(8,8)：(7,6) の上 = 光線 (6,6)
            { ItemID::Poison, 1, 7, "poison" },       // (0,7)(1,6)(1,7)(1,8)
            { ItemID::GoldenArrow, 3, 8, "arrow" },   // (3,8)(4,8)(5,8)
        };
        for (const P& p : layout)
        {
            const int idx = BackpackLogic::Place(bp, p.id, p.r, p.c, 0);
            const ItemCommon* ic = ItemDatabase::GetCommon(p.id);
            snprintf(line, sizeof(line), "shapes place %s at %d,%d -> index %d cells %d influence %d",
                p.name, p.r, p.c, idx, ic ? (int)ic->occupyCells.size() : -1, ic ? (int)ic->influenceCells.size() : -1);
            AutoTestLog(line);
            if (p.id == ItemID::Meteor) s_Meteor = idx;
            if (p.id == ItemID::Beam) s_Beam = idx;
            if (p.id == ItemID::ArcBolt) s_Arc = idx;
        }
        snprintf(line, sizeof(line), "shapes trigger meteor %d beam %d",
            s_Meteor >= 0 ? (int)BackpackLogic::IsTriggerReady(bp, s_Meteor) : -1,
            s_Beam >= 0 ? (int)BackpackLogic::IsTriggerReady(bp, s_Beam) : -1);
        AutoTestLog(line);
        bp.dirty = true;
        m_GameUI.TestShow(1);
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.5f) { AutoTestLog("shapes look backpack"); m_AutoStep = 2; }
    else if (m_AutoStep == 2 && m_AutoTime >= 3.0f)
    {
        if (s_Meteor >= 0) m_GameUI.TestTooltip(s_Meteor, { 820.0f, 260.0f });
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 4.0f) { AutoTestLog("shapes look tooltip"); m_AutoStep = 4; }
    else if (m_AutoStep == 4 && m_AutoTime >= 4.5f)
    {
        m_GameUI.TestTooltip(-1, { 0.0f, 0.0f });
        // 弧を外す → 光線は眠る、戻す → 目覚める（位置は同じ）
        int asleep = -1, awake = -1;
        if (s_Arc >= 0 && s_Beam >= 0)
        {
            const PlacedItem arc = bp.items[s_Arc];
            BackpackLogic::Remove(bp, s_Arc);
            int beam = -1;
            for (int i = 0; i < (int)bp.items.size(); ++i) if (bp.items[i].id == ItemID::Beam) beam = i;
            asleep = (beam >= 0) ? (int)BackpackLogic::IsTriggerReady(bp, beam) : -1;
            BackpackLogic::Place(bp, arc.id, arc.row, arc.col, arc.rotation);
            for (int i = 0; i < (int)bp.items.size(); ++i) if (bp.items[i].id == ItemID::Beam) beam = i;
            awake = (beam >= 0) ? (int)BackpackLogic::IsTriggerReady(bp, beam) : -1;
            bp.dirty = true;
        }
        snprintf(line, sizeof(line), "shapes beam without arc %d, arc back %d", asleep, awake);
        AutoTestLog(line);
        m_AutoStep = 5;
    }
    else if (m_AutoStep == 5 && m_AutoTime >= 5.0f) { AutoTestLog("shapes done"); m_AutoStep = 6; }
}

REGISTER_BATTLE_AUTOTEST("shapes", AutoTestShapes)
