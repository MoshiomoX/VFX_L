// ============================================================
// AutoTestResult.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=result（2026-10-09、リザルトの作り直しの確認）
// 1 秒: 湧き停止・全消し・MP 無限。左上 6x6 に枠 4 枚、shapes と同じ並びで メテオ（十字）の上下左右に
//       ファイアボール・ストーンショット + ホーミングボルト。正面 12m に動かない的 3 体 + 群れ 15 体。金貨 +137。背後から見下ろす
// メテオの爆発が何度か出る → 戦闘シーンが山場を写す（autotest.log に「snapshot ... highlight 1」）
// 8.5 秒: 無敵を外して大きな一撃 → 力尽きた → 3 秒後にリザルト（外のスクリプトが「result dead」から数えて撮る）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Player/ShieldComponent.h"

namespace
{
    class AutoTestResult final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        bool m_LoggedDead = false;
    };
}

void AutoTestResult::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player) && m_AutoStep < 2)
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;   // 四択を出さない
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (!m_Registry.Has<BackpackComponent>(m_Player)) return;
    auto& hp = m_Registry.Get<HealthComponent>(m_Player);

    if (m_AutoStep == 0)
    {
        hp.invincible = true;
        if (m_AutoTime < 1.0f) return;
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float x : { -2.0f, 0.0f, 2.0f })
            m_Swarm.SpawnEnemy(Vector3(pp.x + x, m_Grid.SampleHeight(pp.x + x, pp.z + 12.0f) + gy, pp.z + 12.0f), 100000.0f, 0.0f);
        for (int i = 0; i < 15; ++i)
        {
            const float a = (float)i * 2.39996f;
            const float r = 1.5f + 0.35f * (float)i;
            const float x = pp.x + std::cos(a) * r, z = pp.z + 13.0f + std::sin(a) * r;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 100000.0f, 0.0f);
        }

        // 枠 4 枚（左上 6x6）に shapes と同じ並び：メテオ（十字）の上下左右にファイアボール（2x2）・ストーンショット（L）
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        bp.frames.clear();
        BackpackLogic::RebuildFrameOccupancy(bp);
        for (int r : { 0, 3 })
            for (int c : { 0, 3 })
                BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, ItemID::Meteor, 2, 2, 0);
        BackpackLogic::Place(bp, ItemID::Fireball, 0, 3, 0);
        BackpackLogic::Place(bp, ItemID::StoneShot, 3, 1, 0);
        BackpackLogic::Place(bp, ItemID::HomingBolt, 5, 4, 0);
        bp.dirty = true;
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        if (m_Registry.Has<WalletComponent>(m_Player)) m_Registry.Get<WalletComponent>(m_Player).Earn(137.0f);

        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.distance = 14.0f;
        cam.SetPitch(40.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("result start");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 8.5f)
    {
        // 力尽きる：無敵を外し、盾を空にして大きな一撃
        hp.invincible = false;
        if (m_Registry.Has<ShieldComponent>(m_Player)) m_Registry.Get<ShieldComponent>(m_Player).current = 0.0f;
        if (m_Registry.Has<PlayerStateComponent>(m_Player)) m_Registry.Get<PlayerStateComponent>(m_Player).invincibleTimer = 0.0f;
        PlayerStateSystem::TryApplyHit(m_Registry, m_Player, 1.0e6f);
        AutoTestLog("result dying");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && !m_LoggedDead && m_Registry.Has<PlayerStateComponent>(m_Player)
        && m_Registry.Get<PlayerStateComponent>(m_Player).IsDead())
    {
        m_LoggedDead = true;
        char line[96];
        snprintf(line, sizeof(line), "result dead t %.2f kills %u", m_RunTime, m_Swarm.GetCounters().killCount);
        AutoTestLog(line);
    }
}

REGISTER_BATTLE_AUTOTEST("result", AutoTestResult)
