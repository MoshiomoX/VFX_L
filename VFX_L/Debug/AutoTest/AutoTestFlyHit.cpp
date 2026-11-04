// ============================================================
// AutoTestFlyHit.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=flyhit（VFXL_BOSS_TYPE=beast / djinn）
// 空を飛ぶ Boss に魔法が当たる高さ（2026-11-01、ユーザー：石の守護獣の飛行中、魔法は体の下へ当たっている）。
// 以前の浮きは見た目だけで、位置は地面のままだった。今は位置そのものが高い：
//   1 秒: 湧き停止・全消し・無敵・MP 無限、火球だけ持って動かない Boss を呼ぶ
//   Boss が出たら 8 秒、0.5 秒毎に `flyhit t hover bodyAboveGround hpLost`
//   bodyAboveGround = Boss の位置 - その地面（普段の雑魚は groundY 0.9）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestFlyHit final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        float m_T0 = 0.0f, m_Next = 0.0f;
        float m_Hp0 = 0.0f;
        bool m_Shot = false;
    };
}

void AutoTestFlyHit::Run()
{
    char line[200];
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        ClearBackpackItems(bp);
        const int lo = BackpackComponent::GRID / 2 - 1;
        if (BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0) < 0) AutoTestLog("flyhit place FAILED");
        bp.dirty = true;
        m_Stage.bossHp = 200000.0f;
        m_Stage.RequestBoss(false);
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_Stage.IsBossAlive() && m_Stage.BossPos().LengthSquared() > 0.01f)
    {
        m_T0 = m_AutoTime;
        m_Next = 1.0f;
        m_Hp0 = Swarm::HpFromFixed(m_Swarm.GetBossInfo().hp);
        const Vector3 b = m_Stage.BossPos();
        const Vector3 v(b.x, b.y + 1.0f, b.z);
        m_Camera.SetTargetOverride(&v);
        auto& cam = m_Camera.Camera();
        cam.avoidOcclusion = false;
        cam.distance = 20.0f;
        cam.SetPitch(10.0f);
        cam.SetYaw(cam.GetYaw() + 70.0f);
        cam.SnapToTarget();
        m_GameUI.hideAll = true;
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2)
    {
        const float t = m_AutoTime - m_T0;
        if (t < m_Next) return;
        m_Next += 0.5f;
        const auto& info = m_Swarm.GetBossInfo();
        const float ground = m_Grid.SampleHeight(info.pos[0], info.pos[2]);
        snprintf(line, sizeof(line), "flyhit t %.1f hover %.2f bodyAboveGround %.2f hpLost %.0f",
            t, m_Swarm.GetBomberParams().bossHover, info.pos[1] - ground, m_Hp0 - Swarm::HpFromFixed(info.hp));
        AutoTestLog(line);
        if (!m_Shot && t >= 3.0f) { AutoTestLog("flyhit look air"); m_Shot = true; }
        if (t >= 8.0f) { AutoTestLog("flyhit done"); m_AutoStep = 3; }
    }
}

REGISTER_BATTLE_AUTOTEST("flyhit", AutoTestFlyHit)
