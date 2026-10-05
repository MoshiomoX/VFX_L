// ============================================================
// AutoTestPickup.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=pickup
// VFXL_BATTLE_AUTOTEST=pickup：4 択の画面 → 跳躍回数（空中 2 回、12 → 9 → 6.75）→ 磁石で場の球を全部吸う、を記録
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestPickup final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateFrame(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: 4 択・空中跳び・磁石（VFXL_BATTLE_AUTOTEST=pickup）
// 実時間。Update から呼ぶ（4 択の間は gameplay が止まる）。
// 1 秒: レベルアップの経験値 → 2 秒 "pickup cards N"（4 択の画面を撮る）→ 3 秒 選ぶ・空中 2 回にする
// 4.0 / 4.35 / 4.7 秒: 跳ぶ（地上 → 空中 → 空中）。押した次のフレームの vy を記録（12 → 9 → 6.75 のはず）
// 6〜16 秒: その場で撃たせて球を溜める。16 秒: 足元へ磁石を置いて乗る → 17 / 19 / 21 秒 球の数と経験値。22 秒 done
// ============================================================
void AutoTestPickup::Run(float dt)
{
    static float s_Real = 0.0f;
    static int s_Step = 0;
    static int s_PendingJumpLog = 0;
    s_Real += dt;
    if (!m_Registry.IsValid(m_Player)) return;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    auto& lv = m_Registry.Get<LevelComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
    char line[200];

    // 跳んだ次のフレーム: 上向きの速さを記録
    if (s_PendingJumpLog > 0)
    {
        snprintf(line, sizeof(line), "pickup jump #%d vy %.2f grounded %d airJumpsUsed %d",
            s_PendingJumpLog, rb.velocity.y, rb.isGrounded ? 1 : 0,
            m_Registry.Get<PlayerStateComponent>(m_Player).airJumpsUsed);
        AutoTestLog(line);
        s_PendingJumpLog = 0;
        pcs.testJump = false;
    }

    auto at = [&](float t) { return s_Real >= t && s_Real - dt < t; };

    if (s_Step == 0 && s_Real >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        lv.experience += lv.ExpToNext() + 1.0f;
        s_Step = 1;
    }
    else if (s_Step == 1 && s_Real >= 2.0f)
    {
        std::string names;
        for (ItemID id : lv.pendingChoices)
        {
            const ItemCommon* ic = ItemDatabase::GetCommon(id);
            names += std::string(" ") + (ic ? ic->name : "?");
        }
        snprintf(line, sizeof(line), "pickup cards %zu:%s", lv.pendingChoices.size(), names.c_str());
        AutoTestLog(line);
        s_Step = 2;
    }
    else if (s_Step == 2 && s_Real >= 3.0f)
    {
        if (lv.IsChoosing()) LevelUpSystem::Choose(m_Registry, m_Player, lv.pendingChoices.front());
        m_Registry.Get<PlayerStatsComponent>(m_Player).extraJumps = 2;
        pcs.testInput = true;
        pcs.testMove = Vector2::Zero;
        s_Step = 3;
    }
    else if (s_Step == 3)
    {
        lv.experience = 0.0f;   // 以降は三択で止めない
        int n = 0;
        if (at(4.0f)) n = 1;
        else if (at(4.35f)) n = 2;
        else if (at(4.7f)) n = 3;
        if (n > 0) { pcs.testJump = true; s_PendingJumpLog = n; }

        if (at(16.0f))
        {
            const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
            snprintf(line, sizeof(line), "pickup before magnet: orbs %u level %d exp %.0f",
                m_Swarm.GetCounters().aliveOrbs, lv.level, m_ExpGained);
            AutoTestLog(line);
            m_Pickups.Place(m_Registry, m_Grid, pp, 1.5f, 2.5f);
            const auto ps = m_Pickups.GetPositions();
            if (!ps.empty())
            {
                // 置いた磁石（一番近い物）の上へ
                Vector3 best = ps.front();
                for (const Vector3& p : ps)
                    if (Vector3::DistanceSquared(p, pp) < Vector3::DistanceSquared(best, pp)) best = p;
                auto& tf = m_Registry.Get<TransformComponent>(m_Player);
                tf.position = Vector3(best.x, tf.position.y, best.z);
            }
        }
        if (at(17.0f) || at(19.0f) || at(21.0f))
        {
            snprintf(line, sizeof(line), "pickup after magnet: orbs %u level %d exp %.0f",
                m_Swarm.GetCounters().aliveOrbs, lv.level, m_ExpGained);
            AutoTestLog(line);
        }
        if (at(22.0f)) { AutoTestLog("pickup done"); s_Step = 4; }
    }
}

REGISTER_BATTLE_AUTOTEST("pickup", AutoTestPickup)
